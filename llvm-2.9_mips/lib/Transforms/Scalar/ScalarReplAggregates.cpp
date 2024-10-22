//===- ScalarReplAggregates.cpp - Scalar Replacement of Aggregates --------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This transformation implements the well known scalar replacement of
// aggregates transformation.  This xform breaks up alloca instructions of
// aggregate type (structure or array) into individual alloca instructions for
// each member (if possible).  Then, if possible, it transforms the individual
// alloca instructions into nice clean scalar SSA form.
//
// This combines a simple SRoA algorithm with the Mem2Reg algorithm because
// often interact, especially for C++ programs.  As such, iterating between
// SRoA, then Mem2Reg until we run out of things to promote works well.
//
//===----------------------------------------------------------------------===//

#define DEBUG_TYPE "scalarrepl"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Constants.h"
#include "llvm/DerivedTypes.h"
#include "llvm/Function.h"
#include "llvm/GlobalVariable.h"
#include "llvm/Instructions.h"
#include "llvm/IntrinsicInst.h"
#include "llvm/LLVMContext.h"
#include "llvm/Module.h"
#include "llvm/Pass.h"
#include "llvm/Analysis/DebugInfo.h"
#include "llvm/Analysis/DIBuilder.h"
#include "llvm/Analysis/Dominators.h"
#include "llvm/Analysis/Loads.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/Target/TargetData.h"
#include "llvm/Transforms/Utils/PromoteMemToReg.h"
#include "llvm/Transforms/Utils/Local.h"
#include "llvm/Transforms/Utils/SSAUpdater.h"
#include "llvm/Support/CallSite.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/GetElementPtrTypeIterator.h"
#include "llvm/Support/IRBuilder.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
using namespace llvm;

STATISTIC(NumReplaced,  "Number of allocas broken up");
STATISTIC(NumPromoted,  "Number of allocas promoted");
STATISTIC(NumAdjusted,  "Number of scalar allocas adjusted to allow promotion");
STATISTIC(NumConverted, "Number of aggregates converted to scalar");
STATISTIC(NumGlobals,   "Number of allocas copied from constant global");

namespace {
  struct SROA : public FunctionPass {
    SROA(int T, bool hasDT, char &ID)
      : FunctionPass(ID), HasDomTree(hasDT) {
      if (T == -1)
        SRThreshold = 128;
      else
        SRThreshold = T;
    }

    bool runOnFunction(Function &F);

    bool performScalarRepl(Function &F);
    bool performPromotion(Function &F);

  private:
    bool HasDomTree;
    TargetData *TD;

    /// DeadInsts - Keep track of instructions we have made dead, so that
    /// we can remove them after we are done working.
    SmallVector<Value*, 32> DeadInsts;

    /// AllocaInfo - When analyzing uses of an alloca instruction, this captures
    /// information about the uses.  All these fields are initialized to false
    /// and set to true when something is learned.
    struct AllocaInfo {
      /// The alloca to promote.
      AllocaInst *AI;
      
      /// CheckedPHIs - This is a set of verified PHI nodes, to prevent infinite
      /// looping and avoid redundant work.
      SmallPtrSet<PHINode*, 8> CheckedPHIs;
      
      /// isUnsafe - This is set to true if the alloca cannot be SROA'd.
      bool isUnsafe : 1;

      /// isMemCpySrc - This is true if this aggregate is memcpy'd from.
      bool isMemCpySrc : 1;

      /// isMemCpyDst - This is true if this aggregate is memcpy'd into.
      bool isMemCpyDst : 1;

      /// hasSubelementAccess - This is true if a subelement of the alloca is
      /// ever accessed, or false if the alloca is only accessed with mem
      /// intrinsics or load/store that only access the entire alloca at once.
      bool hasSubelementAccess : 1;
      
      /// hasALoadOrStore - This is true if there are any loads or stores to it.
      /// The alloca may just be accessed with memcpy, for example, which would
      /// not set this.
      bool hasALoadOrStore : 1;
      
      explicit AllocaInfo(AllocaInst *ai)
        : AI(ai), isUnsafe(false), isMemCpySrc(false), isMemCpyDst(false),
          hasSubelementAccess(false), hasALoadOrStore(false) {}
    };

    unsigned SRThreshold;

    void MarkUnsafe(AllocaInfo &I, Instruction *User) {
      I.isUnsafe = true;
      DEBUG(dbgs() << "  Transformation preventing inst: " << *User << '\n');
    }

    bool isSafeAllocaToScalarRepl(AllocaInst *AI);

    void isSafeForScalarRepl(Instruction *I, uint64_t Offset, AllocaInfo &Info);
    void isSafePHISelectUseForScalarRepl(Instruction *User, uint64_t Offset,
                                         AllocaInfo &Info);
    void isSafeGEP(GetElementPtrInst *GEPI, uint64_t &Offset, AllocaInfo &Info);
    void isSafeMemAccess(uint64_t Offset, uint64_t MemSize,
                         Type *MemOpType, bool isStore, AllocaInfo &Info,
                         Instruction *TheAccess, bool AllowWholeAccess);
    bool TypeHasComponent(Type *T, uint64_t Offset, uint64_t Size);
    uint64_t FindElementAndOffset(Type *&T, uint64_t &Offset,
                                  Type *&IdxTy);

    void DoScalarReplacement(AllocaInst *AI,
                             std::vector<AllocaInst*> &WorkList);
    void DeleteDeadInstructions();

    void RewriteForScalarRepl(Instruction *I, AllocaInst *AI, uint64_t Offset,
                              SmallVector<AllocaInst*, 32> &NewElts);
    void RewriteBitCast(BitCastInst *BC, AllocaInst *AI, uint64_t Offset,
                        SmallVector<AllocaInst*, 32> &NewElts);
    void RewriteGEP(GetElementPtrInst *GEPI, AllocaInst *AI, uint64_t Offset,
                    SmallVector<AllocaInst*, 32> &NewElts);
    void RewriteLifetimeIntrinsic(IntrinsicInst *II, AllocaInst *AI,
                                  uint64_t Offset,
                                  SmallVector<AllocaInst*, 32> &NewElts);
    void RewriteMemIntrinUserOfAlloca(MemIntrinsic *MI, Instruction *Inst,
                                      AllocaInst *AI,
                                      SmallVector<AllocaInst*, 32> &NewElts);
    void RewriteStoreUserOfWholeAlloca(StoreInst *SI, AllocaInst *AI,
                                       SmallVector<AllocaInst*, 32> &NewElts);
    void RewriteLoadUserOfWholeAlloca(LoadInst *LI, AllocaInst *AI,
                                      SmallVector<AllocaInst*, 32> &NewElts);

    static MemTransferInst *isOnlyCopiedFromConstantGlobal(
        AllocaInst *AI, SmallVector<Instruction*, 4> &ToDelete);
  };
  
  // SROA_DT - SROA that uses DominatorTree.
  struct SROA_DT : public SROA {
    static char ID;
  public:
    SROA_DT(int T = -1) : SROA(T, true, ID) {
      initializeSROA_DTPass(*PassRegistry::getPassRegistry());
    }
    
    // getAnalysisUsage - This pass does not require any passes, but we know it
    // will not alter the CFG, so say so.
    virtual void getAnalysisUsage(AnalysisUsage &AU) const {
      AU.addRequired<DominatorTree>();
      AU.setPreservesCFG();
    }
  };
  
  // SROA_SSAUp - SROA that uses SSAUpdater.
  struct SROA_SSAUp : public SROA {
    static char ID;
  public:
    SROA_SSAUp(int T = -1) : SROA(T, false, ID) {
      initializeSROA_SSAUpPass(*PassRegistry::getPassRegistry());
    }
    
    // getAnalysisUsage - This pass does not require any passes, but we know it
    // will not alter the CFG, so say so.
    virtual void getAnalysisUsage(AnalysisUsage &AU) const {
      AU.setPreservesCFG();
    }
  };
  
}

char SROA_DT::ID = 0;
char SROA_SSAUp::ID = 0;

INITIALIZE_PASS_BEGIN(SROA_DT, "scalarrepl",
                "Scalar Replacement of Aggregates (DT)", false, false)
INITIALIZE_PASS_DEPENDENCY(DominatorTree)
INITIALIZE_PASS_END(SROA_DT, "scalarrepl",
                "Scalar Replacement of Aggregates (DT)", false, false)

INITIALIZE_PASS_BEGIN(SROA_SSAUp, "scalarrepl-ssa",
                      "Scalar Replacement of Aggregates (SSAUp)", false, false)
INITIALIZE_PASS_END(SROA_SSAUp, "scalarrepl-ssa",
                    "Scalar Replacement of Aggregates (SSAUp)", false, false)

// Public interface to the ScalarReplAggregates pass
FunctionPass *llvm::createScalarReplAggregatesPass(int Threshold,
                                                   bool UseDomTree) {
  if (UseDomTree)
    return new SROA_DT(Threshold);
  return new SROA_SSAUp(Threshold);
}


//===----------------------------------------------------------------------===//
// Convert To Scalar Optimization.
//===----------------------------------------------------------------------===//

namespace {
/// ConvertToScalarInfo - This class implements the "Convert To Scalar"
/// optimization, which scans the uses of an alloca and determines if it can
/// rewrite it in terms of a single new alloca that can be mem2reg'd.
class ConvertToScalarInfo {
  /// AllocaSize - The size of the alloca being considered in bytes.
  unsigned AllocaSize;
  const TargetData &TD;

  /// IsNotTrivial - This is set to true if there is some access to the object
  /// which means that mem2reg can't promote it.
  bool IsNotTrivial;

  /// ScalarKind - Tracks the kind of alloca being considered for promotion,
  /// computed based on the uses of the alloca rather than the LLVM type system.
  enum {
    Unknown,

    // Accesses via GEPs that are consistent with element access of a vector
    // type. This will not be converted into a vector unless there is a later
    // access using an actual vector type.
    ImplicitVector,

    // Accesses via vector operations and GEPs that are consistent with the
    // layout of a vector type.
    Vector,

    // An integer bag-of-bits with bitwise operations for insertion and
    // extraction. Any combination of types can be converted into this kind
    // of scalar.
    Integer
  } ScalarKind;

  /// VectorTy - This tracks the type that we should promote the vector to if
  /// it is possible to turn it into a vector.  This starts out null, and if it
  /// isn't possible to turn into a vector type, it gets set to VoidTy.
  VectorType *VectorTy;

  /// HadNonMemTransferAccess - True if there is at least one access to the 
  /// alloca that is not a MemTransferInst.  We don't want to turn structs into
  /// large integers unless there is some potential for optimization.
  bool HadNonMemTransferAccess;

public:
  explicit ConvertToScalarInfo(unsigned Size, const TargetData &td)
    : AllocaSize(Size), TD(td), IsNotTrivial(false), ScalarKind(Unknown),
      VectorTy(0), HadNonMemTransferAccess(false) { }

  AllocaInst *TryConvert(AllocaInst *AI);

private:
  bool CanConvertToScalar(Value *V, uint64_t Offset);
  void MergeInTypeForLoadOrStore(Type *In, uint64_t Offset);
  bool MergeInVectorType(VectorType *VInTy, uint64_t Offset);
  void ConvertUsesToScalar(Value *Ptr, AllocaInst *NewAI, uint64_t Offset);

  Value *ConvertScalar_ExtractValue(Value *NV, Type *ToType,
                                    uint64_t Offset, IRBuilder<> &Builder);
  Value *ConvertScalar_InsertValue(Value *StoredVal, Value *ExistingVal,
                                   uint64_t Offset, IRBuilder<> &Builder);
};
} // end anonymous namespace.


/// TryConvert - Analyze the specified alloca, and if it is safe to do so,
/// rewrite it to be a new alloca which is mem2reg'able.  This returns the new
/// alloca if possible or null if not.
AllocaInst *ConvertToScalarInfo::TryConvert(AllocaInst *AI) {
  // If we can't convert this scalar, or if mem2reg can trivially do it, bail
  // out.
  if (!CanConvertToScalar(AI, 0) || !IsNotTrivial)
    return 0;

  // If an alloca has only memset / memcpy uses, it may still have an Unknown
  // ScalarKind. Treat it as an Integer below.
  if (ScalarKind == Unknown)
    ScalarKind = Integer;

  // FIXME: It should be possible to promote the vector type up to the alloca's
  // size.
  if (ScalarKind == Vector && VectorTy->getBitWidth() != AllocaSize * 8)
    ScalarKind = Integer;

  // If we were able to find a vector type that can handle this with
  // insert/extract elements, and if there was at least one use that had
  // a vector type, promote this to a vector.  We don't want to promote
  // random stuff that doesn't use vectors (e.g. <9 x double>) because then
  // we just get a lot of insert/extracts.  If at least one vector is
  // involved, then we probably really do have a union of vector/array.
  Type *NewTy;
  if (ScalarKind == Vector) {
    assert(VectorTy && "Missing type for vector scalar.");
    DEBUG(dbgs() << "CONVERT TO VECTOR: " << *AI << "\n  TYPE = "
          << *VectorTy << '\n');
    NewTy = VectorTy;  // Use the vector type.
  } else {
    unsigned BitWidth = AllocaSize * 8;
    if ((ScalarKind == ImplicitVector || ScalarKind == Integer) &&
        !HadNonMemTransferAccess && !TD.fitsInLegalInteger(BitWidth))
      return 0;

    DEBUG(dbgs() << "CONVERT TO SCALAR INTEGER: " << *AI << "\n");
    // Create and insert the integer alloca.
    NewTy = IntegerType::get(AI->getContext(), BitWidth);
  }
  AllocaInst *NewAI = new AllocaInst(NewTy, 0, "", AI->getParent()->begin());
  ConvertUsesToScalar(AI, NewAI, 0);
  return NewAI;
}

/// MergeInTypeForLoadOrStore - Add the 'In' type to the accumulated vector type
/// (VectorTy) so far at the offset specified by Offset (which is specified in
/// bytes).
///
/// There are three cases we handle here:
///   1) A union of vector types of the same size and potentially its elements.
///      Here we turn element accesses into insert/extract element operations.
///      This promotes a <4 x float> with a store of float to the third element
///      into a <4 x float> that uses insert element.
///   2) A union of vector types with power-of-2 size differences, e.g. a float,
///      <2 x float> and <4 x float>.  Here we turn element accesses into insert
///      and extract element operations, and <2 x float> accesses into a cast to
///      <2 x double>, an extract, and a cast back to <2 x float>.
///   3) A fully general blob of memory, which we turn into some (potentially
///      large) integer type with extract and insert operations where the loads
///      and stores would mutate the memory.  We mark this by setting VectorTy
///      to VoidTy.
void ConvertToScalarInfo::MergeInTypeForLoadOrStore(Type *In,
                                                    uint64_t Offset) {
  // If we already decided to turn this into a blob of integer memory, there is
  // nothing to be done.
  if (ScalarKind == Integer)
    return;

  // If this could be contributing to a vector, analyze it.

  // If the In type is a vector that is the same size as the alloca, see if it
  // matches the existing VecTy.
  if (VectorType *VInTy = dyn_cast<VectorType>(In)) {
    if (MergeInVectorType(VInTy, Offset))
      return;
  } else if (In->isFloatTy() || In->isDoubleTy() ||
             (In->isIntegerTy() && In->getPrimitiveSizeInBits() >= 8 &&
              isPowerOf2_32(In->getPrimitiveSizeInBits()))) {
    // Full width accesses can be ignored, because they can always be turned
    // into bitcasts.
    unsigned EltSize = In->getPrimitiveSizeInBits()/8;
    if (EltSize == AllocaSize)
      return;

    // If we're accessing something that could be an element of a vector, see
    // if the implied vector agrees with what we already have and if Offset is
    // compatible with it.
    if (Offset % EltSize == 0 && AllocaSize % EltSize == 0 &&
        (!VectorTy || Offset * 8 < VectorTy->getPrimitiveSizeInBits())) {
      if (!VectorTy) {
        ScalarKind = ImplicitVector;
        VectorTy = VectorType::get(In, AllocaSize/EltSize);
        return;
      }

      unsigned CurrentEltSize = VectorTy->getElementType()
                                ->getPrimitiveSizeInBits()/8;
      if (EltSize == CurrentEltSize)
        return;

      if (In->isIntegerTy() && isPowerOf2_32(AllocaSize / EltSize))
        return;
    }
  }

  // Otherwise, we have a case that we can't handle with an optimized vector
  // form.  We can still turn this into a large integer.
  ScalarKind = Integer;
}

/// MergeInVectorType - Handles the vector case of MergeInTypeForLoadOrStore,
/// returning true if the type was successfully merged and false otherwise.
bool ConvertToScalarInfo::MergeInVectorType(VectorType *VInTy,
                                            uint64_t Offset) {
  // TODO: Support nonzero offsets?
  if (Offset != 0)
    return false;

  // Only allow vectors that are a power-of-2 away from the size of the alloca.
  if (!isPowerOf2_64(AllocaSize / (VInTy->getBitWidth() / 8)))
    return false;

  // If this the first vector we see, remember the type so that we know the
  // element size.
  if (!VectorTy) {
    ScalarKind = Vector;
    VectorTy = VInTy;
    return true;
  }

  unsigned BitWidth = VectorTy->getBitWidth();
  unsigned InBitWidth = VInTy->getBitWidth();

  // Vectors of the same size can be converted using a simple bitcast.
  if (InBitWidth == BitWidth && AllocaSize == (InBitWidth / 8)) {
    ScalarKind = Vector;
    return true;
  }

  Type *ElementTy = VectorTy->getElementType();
  Type *InElementTy = VInTy->getElementType();

  // If they're the same alloc size, we'll be attempting to convert between
  // them with a vector shuffle, which requires the element types to match.
  if (TD.getTypeAllocSize(VectorTy) == TD.getTypeAllocSize(VInTy) &&
      ElementTy != InElementTy)
    return false;

  // Do not allow mixed integer and floating-point accesses from vectors of
  // different sizes.
  if (ElementTy->isFloatingPointTy() != InElementTy->isFloatingPointTy())
    return false;

  if (ElementTy->isFloatingPointTy()) {
    // Only allow floating-point vectors of different sizes if they have the
    // same element type.
    // TODO: This could be loosened a bit, but would anything benefit?
    if (ElementTy != InElementTy)
      return false;

    // There are no arbitrary-precision floating-point types, which limits the
    // number of legal vector types with larger element types that we can form
    // to bitcast and extract a subvector.
    // TODO: We could support some more cases with mixed fp128 and double here.
    if (!(BitWidth == 64 || BitWidth == 128) ||
        !(InBitWidth == 64 || InBitWidth == 128))
      return false;
  } else {
    assert(ElementTy->isIntegerTy() && "Vector elements must be either integer "
                                       "or floating-point.");
    unsigned BitWidth = ElementTy->getPrimitiveSizeInBits();
    unsigned InBitWidth = InElementTy->getPrimitiveSizeInBits();

    // Do not allow integer types smaller than a byte or types whose widths are
    // not a multiple of a byte.
    if (BitWidth < 8 || InBitWidth < 8 ||
        BitWidth % 8 != 0 || InBitWidth % 8 != 0)
      return false;
  }

  // Pick the largest of the two vector types.
  ScalarKind = Vector;
  if (InBitWidth > BitWidth)
    VectorTy = VInTy;

  return true;
}

/// CanConvertToScalar - V is a pointer.  If we can convert the pointee and all
/// its accesses to a single vector type, return true and set VecTy to
/// the new type.  If we could convert the alloca into a single promotable
/// integer, return true but set VecTy to VoidTy.  Further, if the use is not a
/// completely trivial use that mem2reg could promote, set IsNotTrivial.  Offset
/// is the current offset from the base of the alloca being analyzed.
///
/// If we see at least one access to the value that is as a vector type, set the
/// SawVec flag.
bool ConvertToScalarInfo::CanConvertToScalar(Value *V, uint64_t Offset) {
  for (Value::use_iterator UI = V->use_begin(), E = V->use_end(); UI!=E; ++UI) {
    Instruction *User = cast<Instruction>(*UI);

    if (LoadInst *LI = dyn_cast<LoadInst>(User)) {
      // Don't break volatile loads.
      if (LI->isVolatile())
        return false;
      // Don't touch MMX operations.
      if (LI->getType()->isX86_MMXTy())
        return false;
      HadNonMemTransferAccess = true;
      MergeInTypeForLoadOrStore(LI->getType(), Offset);
      continue;
    }

    if (StoreInst *SI = dyn_cast<StoreInst>(User)) {
      // Storing the pointer, not into the value?
      if (SI->getOperand(0) == V || SI->isVolatile()) return false;
      // Don't touch MMX operations.
      if (SI->getOperand(0)->getType()->isX86_MMXTy())
        return false;
      HadNonMemTransferAccess = true;
      MergeInTypeForLoadOrStore(SI->getOperand(0)->getType(), Offset);
      continue;
    }

    if (BitCastInst *BCI = dyn_cast<BitCastInst>(User)) {
      if (!onlyUsedByLifetimeMarkers(BCI))
        IsNotTrivial = true;  // Can't be mem2reg'd.
      if (!CanConvertToScalar(BCI, Offset))
        return false;
      continue;
    }

    if (GetElementPtrInst *GEP = dyn_cast<GetElementPtrInst>(User)) {
      // If this is a GEP with a variable indices, we can't handle it.
      if (!GEP->hasAllConstantIndices())
        return false;

      // Compute the offset that this GEP adds to the pointer.
      SmallVector<Value*, 8> Indices(GEP->op_begin()+1, GEP->op_end());
      uint64_t GEPOffset = TD.getIndexedOffset(GEP->getPointerOperandType(),
                                               Indices);
      // See if all uses can be converted.
      if (!CanConvertToScalar(GEP, Offset+GEPOffset))
        return false;
      IsNotTrivial = true;  // Can't be mem2reg'd.
      HadNonMemTransferAccess = true;
      continue;
    }

    // If this is a constant sized memset of a constant value (e.g. 0) we can
    // handle it.
    if (MemSetInst *MSI = dyn_cast<MemSetInst>(User)) {
      // Store of constant value.
      if (!isa<ConstantInt>(MSI->getValue()))
        return false;

      // Store of constant size.
      ConstantInt *Len = dyn_cast<ConstantInt>(MSI->getLength());
      if (!Len)
        return false;

      // If the size differs from the alloca, we can only convert the alloca to
      // an integer bag-of-bits.
      // FIXME: This should handle all of the cases that are currently accepted
      // as vector element insertions.
      if (Len->getZExtValue() != AllocaSize || Offset != 0)
        ScalarKind = Integer;

      IsNotTrivial = true;  // Can't be mem2reg'd.
      HadNonMemTransferAccess = true;
      continue;
    }

    // If this is a memcpy or memmove into or out of the whole allocation, we
    // can handle it like a load or store of the scalar type.
    if (MemTransferInst *MTI = dyn_cast<MemTransferInst>(User)) {
      ConstantInt *Len = dyn_cast<ConstantInt>(MTI->getLength());
      if (Len == 0 || Len->getZExtValue() != AllocaSize || Offset != 0)
        return false;

      IsNotTrivial = true;  // Can't be mem2reg'd.
      continue;
    }

    // If this is a lifetime intrinsic, we can handle it.
    if (IntrinsicInst *II = dyn_cast<IntrinsicInst>(User)) {
      if (II->getIntrinsicID() == Intrinsic::lifetime_start ||
          II->getIntrinsicID() == Intrinsic::lifetime_end) {
        continue;
      }
    }

    // Otherwise, we cannot handle this!
    return false;
  }

  return true;
}

/// ConvertUsesToScalar - Convert all of the users of Ptr to use the new alloca
/// directly.  This happens when we are converting an "integer union" to a
/// single integer scalar, or when we are converting a "vector union" to a
/// vector with insert/extractelement instructions.
///
/// Offset is an offset from the original alloca, in bits that need to be
/// shifted to the right.  By the end of this, there should be no uses of Ptr.
void ConvertToScalarInfo::ConvertUsesToScalar(Value *Ptr, AllocaInst *NewAI,
                                              uint64_t Offset) {
  while (!Ptr->use_empty()) {
    Instruction *User = cast<Instruction>(Ptr->use_back());

    if (BitCastInst *CI = dyn_cast<BitCastInst>(User)) {
      ConvertUsesToScalar(CI, NewAI, Offset);
      CI->eraseFromParent();
      continue;
    }

    if (GetElementPtrInst *GEP = dyn_cast<GetElementPtrInst>(User)) {
      // Compute the offset that this GEP adds to the pointer.
      SmallVector<Value*, 8> Indices(GEP->op_begin()+1, GEP->op_end());
      uint64_t GEPOffset = TD.getIndexedOffset(GEP->getPointerOperandType(),
                                               Indices);
      ConvertUsesToScalar(GEP, NewAI, Offset+GEPOffset*8);
      GEP->eraseFromParent();
      continue;
    }

    IRBuilder<> Builder(User);

    if (LoadInst *LI = dyn_cast<LoadInst>(User)) {
      // The load is a bit extract from NewAI shifted right by Offset bits.
      Value *LoadedVal = Builder.CreateLoad(NewAI, "tmp");
      Value *NewLoadVal
        = ConvertScalar_ExtractValue(LoadedVal, LI->getType(), Offset, Builder);
      LI->replaceAllUsesWith(NewLoadVal);
      LI->eraseFromParent();
      continue;
    }

    if (StoreInst *SI = dyn_cast<StoreInst>(User)) {
      assert(SI->getOperand(0) != Ptr && "Consistency error!");
      Instruction *Old = Builder.CreateLoad(NewAI, NewAI->getName()+".in");
      Value *New = ConvertScalar_InsertValue(SI->getOperand(0), Old, Offset,
                                             Builder);
      Builder.CreateStore(New, NewAI);
      SI->eraseFromParent();

      // If the load we just inserted is now dead, then the inserted store
      // overwrote the entire thing.
      if (Old->use_empty())
        Old->eraseFromParent();
      continue;
    }

    // If this is a constant sized memset of a constant value (e.g. 0) we can
    // transform it into a store of the expanded constant value.
    if (MemSetInst *MSI = dyn_cast<MemSetInst>(User)) {
      assert(MSI->getRawDest() == Ptr && "Consistency error!");
      unsigned NumBytes = cast<ConstantInt>(MSI->getLength())->getZExtValue();
      if (NumBytes != 0) {
        unsigned Val = cast<ConstantInt>(MSI->getValue())->getZExtValue();

        // Compute the value replicated the right number of times.
        APInt APVal(NumBytes*8, Val);

        // Splat the value if non-zero.
        if (Val)
          for (unsigned i = 1; i != NumBytes; ++i)
            APVal |= APVal << 8;

        Instruction *Old = Builder.CreateLoad(NewAI, NewAI->getName()+".in");
        Value *New = ConvertScalar_InsertValue(
                                    ConstantInt::get(User->getContext(), APVal),
                                               Old, Offset, Builder);
        Builder.CreateStore(New, NewAI);

        // If the load we just inserted is now dead, then the memset overwrote
        // the entire thing.
        if (Old->use_empty())
          Old->eraseFromParent();
      }
      MSI->eraseFromParent();
      continue;
    }

    // If this is a memcpy or memmove into or out of the whole allocation, we
    // can handle it like a load or store of the scalar type.
    if (MemTransferInst *MTI = dyn_cast<MemTransferInst>(User)) {
      assert(Offset == 0 && "must be store to start of alloca");

      // If the source and destination are both to the same alloca, then this is
      // a noop copy-to-self, just delete it.  Otherwise, emit a load and store
      // as appropriate.
      AllocaInst *OrigAI = cast<AllocaInst>(GetUnderlyingObject(Ptr, &TD, 0));

      if (GetUnderlyingObject(MTI->getSource(), &TD, 0) != OrigAI) {
        // Dest must be OrigAI, change this to be a load from the original
        // pointer (bitcasted), then a store to our new alloca.
        assert(MTI->getRawDest() == Ptr && "Neither use is of pointer?");
        Value *SrcPtr = MTI->getSource();
        PointerType* SPTy = cast<PointerType>(SrcPtr->getType());
        PointerType* AIPTy = cast<PointerType>(NewAI->getType());
        if (SPTy->getAddressSpace() != AIPTy->getAddressSpace()) {
          AIPTy = PointerType::get(AIPTy->getElementType(),
                                   SPTy->getAddressSpace());
        }
        SrcPtr = Builder.CreateBitCast(SrcPtr, AIPTy);

        LoadInst *SrcVal = Builder.CreateLoad(SrcPtr, "srcval");
        SrcVal->setAlignment(MTI->getAlignment());
        Builder.CreateStore(SrcVal, NewAI);
      } else if (GetUnderlyingObject(MTI->getDest(), &TD, 0) != OrigAI) {
        // Src must be OrigAI, change this to be a load from NewAI then a store
        // through the original dest pointer (bitcasted).
        assert(MTI->getRawSource() == Ptr && "Neither use is of pointer?");
        LoadInst *SrcVal = Builder.CreateLoad(NewAI, "srcval");

        PointerType* DPTy = cast<PointerType>(MTI->getDest()->getType());
        PointerType* AIPTy = cast<PointerType>(NewAI->getType());
        if (DPTy->getAddressSpace() != AIPTy->getAddressSpace()) {
          AIPTy = PointerType::get(AIPTy->getElementType(),
                                   DPTy->getAddressSpace());
        }
        Value *DstPtr = Builder.CreateBitCast(MTI->getDest(), AIPTy);

        StoreInst *NewStore = Builder.CreateStore(SrcVal, DstPtr);
        NewStore->setAlignment(MTI->getAlignment());
      } else {
        // Noop transfer. Src == Dst
      }

      MTI->eraseFromParent();
      continue;
    }

    if (IntrinsicInst *II = dyn_cast<IntrinsicInst>(User)) {
      if (II->getIntrinsicID() == Intrinsic::lifetime_start ||
          II->getIntrinsicID() == Intrinsic::lifetime_end) {
        // There's no need to preserve these, as the resulting alloca will be
        // converted to a register anyways.
        II->eraseFromParent();
        continue;
      }
    }

    llvm_unreachable("Unsupported operation!");
  }
}

/// getScaledElementType - Gets a scaled element type for a partial vector
/// access of an alloca. The input types must be integer or floating-point
/// scalar or vector types, and the resulting type is an integer, float or
/// double.
static Type *getScaledElementType(Type *Ty1, Type *Ty2,
                                        unsigned NewBitWidth) {
  bool IsFP1 = Ty1->isFloatingPointTy() ||
               (Ty1->isVectorTy() &&
                cast<VectorType>(Ty1)->getElementType()->isFloatingPointTy());
  bool IsFP2 = Ty2->isFloatingPointTy() ||
               (Ty2->isVectorTy() &&
                cast<VectorType>(Ty2)->getElementType()->isFloatingPointTy());

  LLVMContext &Context = Ty1->getContext();

  // Prefer floating-point types over integer types, as integer types may have
  // been created by earlier scalar replacement.
  if (IsFP1 || IsFP2) {
    if (NewBitWidth == 32)
      return Type::getFloatTy(Context);
    if (NewBitWidth == 64)
      return Type::getDoubleTy(Context);
  }

  return Type::getIntNTy(Context, NewBitWidth);
}

/// CreateShuffleVectorCast - Creates a shuffle vector to convert one vector
/// to another vector of the same element type which has the same allocation
/// size but different primitive sizes (e.g. <3 x i32> and <4 x i32>).
static Value *CreateShuffleVectorCast(Value *FromVal, Type *ToType,
                                      IRBuilder<> &Builder) {
  Type *FromType = FromVal->getType();
  VectorType *FromVTy = cast<VectorType>(FromType);
  VectorType *ToVTy = cast<VectorType>(ToType);
  assert((ToVTy->getElementType() == FromVTy->getElementType()) &&
         "Vectors must have the same element type");
   Value *UnV = UndefValue::get(FromType);
   unsigned numEltsFrom = FromVTy->getNumElements();
   unsigned numEltsTo = ToVTy->getNumElements();

   SmallVector<Constant*, 3> Args;
   Type* Int32Ty = Builder.getInt32Ty();
   unsigned minNumElts = std::min(numEltsFrom, numEltsTo);
   unsigned i;
   for (i=0; i != minNumElts; ++i)
     Args.push_back(ConstantInt::get(Int32Ty, i));

   if (i < numEltsTo) {
     Constant* UnC = UndefValue::get(Int32Ty);
     for (; i != numEltsTo; ++i)
       Args.push_back(UnC);
   }
   Constant *Mask = ConstantVector::get(Args);
   return Builder.CreateShuffleVector(FromVal, UnV, Mask, "tmpV");
}

/// ConvertScalar_ExtractValue - Extract a value of type ToType from an integer
/// or vector value FromVal, extracting the bits from the offset specified by
/// Offset.  This returns the value, which is of type ToType.
///
/// This happens when we are converting an "integer union" to a single
/// integer scalar, or when we are converting a "vector union" to a vector with
/// insert/extractelement instructions.
///
/// Offset is an offset from the original alloca, in bits that need to be
/// shifted to the right.
Value *ConvertToScalarInfo::
ConvertScalar_ExtractValue(Value *FromVal, Type *ToType,
                           uint64_t Offset, IRBuilder<> &Builder) {
  // If the load is of the whole new alloca, no conversion is needed.
  Type *FromType = FromVal->getType();
  if (FromType == ToType && Offset == 0)
    return FromVal;

  // If the result alloca is a vector type, this is either an element
  // access or a bitcast to another vector type of the same size.
  if (VectorType *VTy = dyn_cast<VectorType>(FromType)) {
    unsigned FromTypeSize = TD.getTypeAllocSize(FromType);
    unsigned ToTypeSize = TD.getTypeAllocSize(ToType);
    if (FromTypeSize == ToTypeSize) {
      // If the two types have the same primitive size, use a bit cast.
      // Otherwise, it is two vectors with the same element type that has
      // the same allocation size but different number of elements so use
      // a shuffle vector.
      if (FromType->getPrimitiveSizeInBits() ==
          ToType->getPrimitiveSizeInBits())
        return Builder.CreateBitCast(FromVal, ToType, "tmp");
      else
        return CreateShuffleVectorCast(FromVal, ToType, Builder);
    }

    if (isPowerOf2_64(FromTypeSize / ToTypeSize)) {
      assert(!(ToType->isVectorTy() && Offset != 0) && "Can't extract a value "
             "of a smaller vector type at a nonzero offset.");

      Type *CastElementTy = getScaledElementType(FromType, ToType,
                                                       ToTypeSize * 8);
      unsigned NumCastVectorElements = FromTypeSize / ToTypeSize;

      LLVMContext &Context = FromVal->getContext();
      Type *CastTy = VectorType::get(CastElementTy,
                                           NumCastVectorElements);
      Value *Cast = Builder.CreateBitCast(FromVal, CastTy, "tmp");

      unsigned EltSize = TD.getTypeAllocSizeInBits(CastElementTy);
      unsigned Elt = Offset/EltSize;
      assert(EltSize*Elt == Offset && "Invalid modulus in validity checking");
      Value *Extract = Builder.CreateExtractElement(Cast, ConstantInt::get(
                                        Type::getInt32Ty(Context), Elt), "tmp");
      return Builder.CreateBitCast(Extract, ToType, "tmp");
    }

    // Otherwise it must be an element access.
    unsigned Elt = 0;
    if (Offset) {
      unsigned EltSize = TD.getTypeAllocSizeInBits(VTy->getElementType());
      Elt = Offset/EltSize;
      assert(EltSize*Elt == Offset && "Invalid modulus in validity checking");
    }
    // Return the element extracted out of it.
    Value *V = Builder.CreateExtractElement(FromVal, ConstantInt::get(
                    Type::getInt32Ty(FromVal->getContext()), Elt), "tmp");
    if (V->getType() != ToType)
      V = Builder.CreateBitCast(V, ToType, "tmp");
    return V;
  }

  // If ToType is a first class aggregate, extract out each of the pieces and
  // use insertvalue's to form the FCA.
  if (StructType *ST = dyn_cast<StructType>(ToType)) {
    const StructLayout &Layout = *TD.getStructLayout(ST);
    Value *Res = UndefValue::get(ST);
    for (unsigned i = 0, e = ST->getNumElements(); i != e; ++i) {
      Value *Elt = ConvertScalar_ExtractValue(FromVal, ST->getElementType(i),
                                        Offset+Layout.getElementOffsetInBits(i),
                                              Builder);
      Res = Builder.CreateInsertValue(Res, Elt, i, "tmp");
    }
    return Res;
  }

  if (ArrayType *AT = dyn_cast<ArrayType>(ToType)) {
    uint64_t EltSize = TD.getTypeAllocSizeInBits(AT->getElementType());
    Value *Res = UndefValue::get(AT);
    for (unsigned i = 0, e = AT->getNumElements(); i != e; ++i) {
      Value *Elt = ConvertScalar_ExtractValue(FromVal, AT->getElementType(),
                                              Offset+i*EltSize, Builder);
      Res = Builder.CreateInsertValue(Res, Elt, i, "tmp");
    }
    return Res;
  }

  // Otherwise, this must be a union that was converted to an integer value.
  IntegerType *NTy = cast<IntegerType>(FromVal->getType());

  // If this is a big-endian system and the load is narrower than the
  // full alloca type, we need to do a shift to get the right bits.
  int ShAmt = 0;
  if (TD.isBigEndian()) {
    // On big-endian machines, the lowest bit is stored at the bit offset
    // from the pointer given by getTypeStoreSizeInBits.  This matters for
    // integers with a bitwidth that is not a multiple of 8.
    ShAmt = TD.getTypeStoreSizeInBits(NTy) -
            TD.getTypeStoreSizeInBits(ToType) - Offset;
  } else {
    ShAmt = Offset;
  }

  // Note: we support negative bitwidths (with shl) which are not defined.
  // We do this to support (f.e.) loads off the end of a structure where
  // only some bits are used.
  if (ShAmt > 0 && (unsigned)ShAmt < NTy->getBitWidth())
    FromVal = Builder.CreateLShr(FromVal,
                                 ConstantInt::get(FromVal->getType(),
                                                           ShAmt), "tmp");
  else if (ShAmt < 0 && (unsigned)-ShAmt < NTy->getBitWidth())
    FromVal = Builder.CreateShl(FromVal,
                                ConstantInt::get(FromVal->getType(),
                                                          -ShAmt), "tmp");

  // Finally, unconditionally truncate the integer to the right width.
  unsigned LIBitWidth = TD.getTypeSizeInBits(ToType);
  if (LIBitWidth < NTy->getBitWidth())
    FromVal =
      Builder.CreateTrunc(FromVal, IntegerType::get(FromVal->getContext(),
                                                    LIBitWidth), "tmp");
  else if (LIBitWidth > NTy->getBitWidth())
    FromVal =
       Builder.CreateZExt(FromVal, IntegerType::get(FromVal->getContext(),
                                                    LIBitWidth), "tmp");

  // If the result is an integer, this is a trunc or bitcast.
  if (ToType->isIntegerTy()) {
    // Should be done.
  } else if (ToType->isFloatingPointTy() || ToType->isVectorTy()) {
    // Just do a bitcast, we know the sizes match up.
    FromVal = Builder.CreateBitCast(FromVal, ToType, "tmp");
  } else {
    // Otherwise must be a pointer.
    FromVal = Builder.CreateIntToPtr(FromVal, ToType, "tmp");
  }
  assert(FromVal->getType() == ToType && "Didn't convert right?");
  return FromVal;
}

/// ConvertScalar_InsertValue - Insert the value "SV" into the existing integer
/// or vector value "Old" at the offset specified by Offset.
///
/// This happens when we are converting an "integer union" to a
/// single integer scalar, or when we are converting a "vector union" to a
/// vector with insert/extractelement instructions.
///
/// Offset is an offset from the original alloca, in bits that need to be
/// shifted to the right.
Value *ConvertToScalarInfo::
ConvertScalar_InsertValue(Value *SV, Value *Old,
                          uint64_t Offset, IRBuilder<> &Builder) {
  // Convert the stored type to the actual type, shift it left to insert
  // then 'or' into place.
  Type *AllocaType = Old->getType();
  LLVMContext &Context = Old->getContext();

  if (VectorType *VTy = dyn_cast<VectorType>(AllocaType)) {
    uint64_t VecSize = TD.getTypeAllocSizeInBits(VTy);
    uint64_t ValSize = TD.getTypeAllocSizeInBits(SV->getType());

    // Changing the whole vector with memset or with an access of a different
    // vector type?
    if (ValSize == VecSize) {
      // If the two types have the same primitive size, use a bit cast.
      // Otherwise, it is two vectors with the same element type that has
      // the same allocation size but different number of elements so use
      // a shuffle vector.
      if (VTy->getPrimitiveSizeInBits() ==
          SV->getType()->getPrimitiveSizeInBits())
        return Builder.CreateBitCast(SV, AllocaType, "tmp");
      else
        return CreateShuffleVectorCast(SV, VTy, Builder);
    }

    if (isPowerOf2_64(VecSize / ValSize)) {
      assert(!(SV->getType()->isVectorTy() && Offset != 0) && "Can't insert a "
             "value of a smaller vector type at a nonzero offset.");

      Type *CastElementTy = getScaledElementType(VTy, SV->getType(),
                                                       ValSize);
      unsigned NumCastVectorElements = VecSize / ValSize;

      LLVMContext &Context = SV->getContext();
      Type *OldCastTy = VectorType::get(CastElementTy,
                                              NumCastVectorElements);
      Value *OldCast = Builder.CreateBitCast(Old, OldCastTy, "tmp");

      Value *SVCast = Builder.CreateBitCast(SV, CastElementTy, "tmp");

      unsigned EltSize = TD.getTypeAllocSizeInBits(CastElementTy);
      unsigned Elt = Offset/EltSize;
      assert(EltSize*Elt == Offset && "Invalid modulus in validity checking");
      Value *Insert =
        Builder.CreateInsertElement(OldCast, SVCast, ConstantInt::get(
                                        Type::getInt32Ty(Context), Elt), "tmp");
      return Builder.CreateBitCast(Insert, AllocaType, "tmp");
    }

    // Must be an element insertion.
    assert(SV->getType() == VTy->getElementType());
    uint64_t EltSize = TD.getTypeAllocSizeInBits(VTy->getElementType());
    unsigned Elt = Offset/EltSize;
    return Builder.CreateInsertElement(Old, SV,
                     ConstantInt::get(Type::getInt32Ty(SV->getContext()), Elt),
                                     "tmp");
  }

  // If SV is a first-class aggregate value, insert each value recursively.
  if (StructType *ST = dyn_cast<StructType>(SV->getType())) {
    const StructLayout &Layout = *TD.getStructLayout(ST);
    for (unsigned i = 0, e = ST->getNumElements(); i != e; ++i) {
      Value *Elt = Builder.CreateExtractValue(SV, i, "tmp");
      Old = ConvertScalar_InsertValue(Elt, Old,
                                      Offset+Layout.getElementOffsetInBits(i),
                                      Builder);
    }
    return Old;
  }

  if (ArrayType *AT = dyn_cast<ArrayType>(SV->getType())) {
    uint64_t EltSize = TD.getTypeAllocSizeInBits(AT->getElementType());
    for (unsigned i = 0, e = AT->getNumElements(); i != e; ++i) {
      Value *Elt = Builder.CreateExtractValue(SV, i, "tmp");
      Old = ConvertScalar_InsertValue(Elt, Old, Offset+i*EltSize, Builder);
    }
    return Old;
  }

  // If SV is a float, convert it to the appropriate integer type.
  // If it is a pointer, do the same.
  unsigned SrcWidth = TD.getTypeSizeInBits(SV->getType());
  unsigned DestWidth = TD.getTypeSizeInBits(AllocaType);
  unsigned SrcStoreWidth = TD.getTypeStoreSizeInBits(SV->getType());
  unsigned DestStoreWidth = TD.getTypeStoreSizeInBits(AllocaType);
  if (SV->getType()->isFloatingPointTy() || SV->getType()->isVectorTy())
    SV = Builder.CreateBitCast(SV,
                            IntegerType::get(SV->getContext(),SrcWidth), "tmp");
  else if (SV->getType()->isPointerTy())
    SV = Builder.CreatePtrToInt(SV, TD.getIntPtrType(SV->getContext()), "tmp");

  // Zero extend or truncate the value if needed.
  if (SV->getType() != AllocaType) {
    if (SV->getType()->getPrimitiveSizeInBits() <
             AllocaType->getPrimitiveSizeInBits())
      SV = Builder.CreateZExt(SV, AllocaType, "tmp");
    else {
      // Truncation may be needed if storing more than the alloca can hold
      // (undefined behavior).
      SV = Builder.CreateTrunc(SV, AllocaType, "tmp");
      SrcWidth = DestWidth;
      SrcStoreWidth = DestStoreWidth;
    }
  }

  // If this is a big-endian system and the store is narrower than the
  // full alloca type, we need to do a shift to get the right bits.
  int ShAmt = 0;
  if (TD.isBigEndian()) {
    // On big-endian machines, the lowest bit is stored at the bit offset
    // from the pointer given by getTypeStoreSizeInBits.  This matters for
    // integers with a bitwidth that is not a multiple of 8.
    ShAmt = DestStoreWidth - SrcStoreWidth - Offset;
  } else {
    ShAmt = Offset;
  }

  // Note: we support negative bitwidths (with shr) which are not defined.
  // We do this to support (f.e.) stores off the end of a structure where
  // only some bits in the structure are set.
  APInt Mask(APInt::getLowBitsSet(DestWidth, SrcWidth));
  if (ShAmt > 0 && (unsigned)ShAmt < DestWidth) {
    SV = Builder.CreateShl(SV, ConstantInt::get(SV->getType(),
                           ShAmt), "tmp");
    Mask <<= ShAmt;
  } else if (ShAmt < 0 && (unsigned)-ShAmt < DestWidth) {
    SV = Builder.CreateLShr(SV, ConstantInt::get(SV->getType(),
                            -ShAmt), "tmp");
    Mask = Mask.lshr(-ShAmt);
  }

  // Mask out the bits we are about to insert from the old value, and or
  // in the new bits.
  if (SrcWidth != DestWidth) {
    assert(DestWidth > SrcWidth);
    Old = Builder.CreateAnd(Old, ConstantInt::get(Context, ~Mask), "mask");
    SV = Builder.CreateOr(Old, SV, "ins");
  }
  return SV;
}


//===----------------------------------------------------------------------===//
// SRoA Driver
//===----------------------------------------------------------------------===//


bool SROA::runOnFunction(Function &F) {
  TD = getAnalysisIfAvailable<TargetData>();

  bool Changed = performPromotion(F);

  // FIXME: ScalarRepl currently depends on TargetData more than it
  // theoretically needs to. It should be refactored in order to support
  // target-independent IR. Until this is done, just skip the actual
  // scalar-replacement portion of this pass.
  if (!TD) return Changed;

  while (1) {
    bool LocalChange = performScalarRepl(F);
    if (!LocalChange) break;   // No need to repromote if no scalarrepl
    Changed = true;
    LocalChange = performPromotion(F);
    if (!LocalChange) break;   // No need to re-scalarrepl if no promotion
  }

  return Changed;
}

namespace {
class AllocaPromoter : public LoadAndStorePromoter {
  AllocaInst *AI;
  DIBuilder *DIB;
  SmallVector<DbgDeclareInst *, 4> DDIs;
  SmallVector<DbgValueInst *, 4> DVIs;
public:
  AllocaPromoter(const SmallVectorImpl<Instruction*> &Insts, SSAUpdater &S,
                 DIBuilder *DB)
    : LoadAndStorePromoter(Insts, S), AI(0), DIB(DB) {}
  
  void run(AllocaInst *AI, const SmallVectorImpl<Instruction*> &Insts) {
    // Remember which alloca we're promoting (for isInstInList).
    this->AI = AI;
    if (MDNode *DebugNode = MDNode::getIfExists(AI->getContext(), AI))
      for (Value::use_iterator UI = DebugNode->use_begin(),
             E = DebugNode->use_end(); UI != E; ++UI)
        if (DbgDeclareInst *DDI = dyn_cast<DbgDeclareInst>(*UI))
          DDIs.push_back(DDI);
        else if (DbgValueInst *DVI = dyn_cast<DbgValueInst>(*UI))
          DVIs.push_back(DVI);

    LoadAndStorePromoter::run(Insts);
    AI->eraseFromParent();
    for (SmallVector<DbgDeclareInst *, 4>::iterator I = DDIs.begin(), 
           E = DDIs.end(); I != E; ++I) {
      DbgDeclareInst *DDI = *I;
      DDI->eraseFromParent();
    }
    for (SmallVector<DbgValueInst *, 4>::iterator I = DVIs.begin(), 
           E = DVIs.end(); I != E; ++I) {
      DbgValueInst *DVI = *I;
      DVI->eraseFromParent();
    }
  }
  
  virtual bool isInstInList(Instruction *I,
                            const SmallVectorImpl<Instruction*> &Insts) const {
    if (LoadInst *LI = dyn_cast<LoadInst>(I))
      return LI->getOperand(0) == AI;
    return cast<StoreInst>(I)->getPointerOperand() == AI;
  }

  virtual void updateDebugInfo(Instruction *Inst) const {
    for (SmallVector<DbgDeclareInst *, 4>::const_iterator I = DDIs.begin(), 
           E = DDIs.end(); I != E; ++I) {
      DbgDeclareInst *DDI = *I;
      if (StoreInst *SI = dyn_cast<StoreInst>(Inst))
        ConvertDebugDeclareToDebugValue(DDI, SI, *DIB);
      else if (LoadInst *LI = dyn_cast<LoadInst>(Inst))
        ConvertDebugDeclareToDebugValue(DDI, LI, *DIB);
    }
    for (SmallVector<DbgValueInst *, 4>::const_iterator I = DVIs.begin(), 
           E = DVIs.end(); I != E; ++I) {
      DbgValueInst *DVI = *I;
      if (StoreInst *SI = dyn_cast<StoreInst>(Inst)) {
        Instruction *DbgVal = NULL;
        // If an argument is zero extended then use argument directly. The ZExt
        // may be zapped by an optimization pass in future.
        Argument *ExtendedArg = NULL;
        if (ZExtInst *ZExt = dyn_cast<ZExtInst>(SI->getOperand(0)))
          ExtendedArg = dyn_cast<Argument>(ZExt->getOperand(0));
        if (SExtInst *SExt = dyn_cast<SExtInst>(SI->getOperand(0)))
          ExtendedArg = dyn_cast<Argument>(SExt->getOperand(0));
        if (ExtendedArg)
          DbgVal = DIB->insertDbgValueIntrinsic(ExtendedArg, 0, 
                                                DIVariable(DVI->getVariable()),
                                                SI);
        else
          DbgVal = DIB->insertDbgValueIntrinsic(SI->getOperand(0), 0, 
                                                DIVariable(DVI->getVariable()),
                                                SI);
        DbgVal->setDebugLoc(DVI->getDebugLoc());
      } else if (LoadInst *LI = dyn_cast<LoadInst>(Inst)) {
        Instruction *DbgVal = 
          DIB->insertDbgValueIntrinsic(LI->getOperand(0), 0, 
                                       DIVariable(DVI->getVariable()), LI);
        DbgVal->setDebugLoc(DVI->getDebugLoc());
      }
    }
  }
};
} // end anon namespace

/// isSafeSelectToSpeculate - Select instructions that use an alloca and are
/// subsequently loaded can be rewritten to load both input pointers and then
/// select between the result, allowing the load of the alloca to be promoted.
/// From this:
///   %P2 = select i1 %cond, i32* %Alloca, i32* %Other
///   %V = load i32* %P2
/// to:
///   %V1 = load i32* %Alloca      -> will be mem2reg'd
///   %V2 = load i32* %Other
///   %V = select i1 %cond, i32 %V1, i32 %V2
///
/// We can do this to a select if its only uses are loads and if the operand to
/// the select can be loaded unconditionally.
static bool isSafeSelectToSpeculate(SelectInst *SI, const TargetData *TD) {
  bool TDerefable = SI->getTrueValue()->isDereferenceablePointer();
  bool FDerefable = SI->getFalseValue()->isDereferenceablePointer();
  
  for (Value::use_iterator UI = SI->use_begin(), UE = SI->use_end();
       UI != UE; ++UI) {
    LoadInst *LI = dyn_cast<LoadInst>(*UI);
    if (LI == 0 || LI->isVolatile()) return false;
    
    // Both operands to the select need to be dereferencable, either absolutely
    // (e.g. allocas) or at this point because we can see other accesses to it.
    if (!TDerefable && !isSafeToLoadUnconditionally(SI->getTrueValue(), LI,
                                                    LI->getAlignment(), TD))
      return false;
    if (!FDerefable && !isSafeToLoadUnconditionally(SI->getFalseValue(), LI,
                                                    LI->getAlignment(), TD))
      return false;
  }
  
  return true;
}

/// isSafePHIToSpeculate - PHI instructions that use an alloca and are
/// subsequently loaded can be rewritten to load both input pointers in the pred
/// blocks and then PHI the results, allowing the load of the alloca to be
/// promoted.
/// From this:
///   %P2 = phi [i32* %Alloca, i32* %Other]
///   %V = load i32* %P2
/// to:
///   %V1 = load i32* %Alloca      -> will be mem2reg'd
///   ...
///   %V2 = load i32* %Other
///   ...
///   %V = phi [i32 %V1, i32 %V2]
///
/// We can do this to a select if its only uses are loads and if the operand to
/// the select can be loaded unconditionally.
static bool isSafePHIToSpeculate(PHINode *PN, const TargetData *TD) {
  // For now, we can only do this promotion if the load is in the same block as
  // the PHI, and if there are no stores between the phi and load.
  // TODO: Allow recursive phi users.
  // TODO: Allow stores.
  BasicBlock *BB = PN->getParent();
  unsigned MaxAlign = 0;
  for (Value::use_iterator UI = PN->use_begin(), UE = PN->use_end();
       UI != UE; ++UI) {
    LoadInst *LI = dyn_cast<LoadInst>(*UI);
    if (LI == 0 || LI->isVolatile()) return false;
    
    // For now we only allow loads in the same block as the PHI.  This is a
    // common case that happens when instcombine merges two loads through a PHI.
    if (LI->getParent() != BB) return false;
    
    // Ensure that there are no instructions between the PHI and the load that
    // could store.
    for (BasicBlock::iterator BBI = PN; &*BBI != LI; ++BBI)
      if (BBI->mayWriteToMemory())
        return false;
    
    MaxAlign = std::max(MaxAlign, LI->getAlignment());
  }
  
  // Okay, we know that we have one or more loads in the same block as the PHI.
  // We can transform this if it is safe to push the loads into the predecessor
  // blocks.  The only thing to watch out for is that we can't put a possibly
  // trapping load in the predecessor if it is a critical edge.
  for (unsigned i = 0, e = PN->getNumIncomingValues(); i != e; ++i) {
    BasicBlock *Pred = PN->getIncomingBlock(i);

    // If the predecessor has a single successor, then the edge isn't critical.
    if (Pred->getTerminator()->getNumSuccessors() == 1)
      continue;
    
    Value *InVal = PN->getIncomingValue(i);
    
    // If the InVal is an invoke in the pred, we can't put a load on the edge.
    if (InvokeInst *II = dyn_cast<InvokeInst>(InVal))
      if (II->getParent() == Pred)
        return false;

    // If this pointer is always safe to load, or if we can prove that there is
    // already a load in the block, then we can move the load to the pred block.
    if (InVal->isDereferenceablePointer() ||
        isSafeToLoadUnconditionally(InVal, Pred->getTerminator(), MaxAlign, TD))
      continue;
    
    return false;
  }
    
  return true;
}


/// tryToMakeAllocaBePromotable - This returns true if the alloca only has
/// direct (non-volatile) loads and stores to it.  If the alloca is close but
/// not quite there, this will transform the code to allow promotion.  As such,
/// it is a non-pure predicate.
static bool tryToMakeAllocaBePromotable(AllocaInst *AI, const TargetData *TD) {
  SetVector<Instruction*, SmallVector<Instruction*, 4>,
            SmallPtrSet<Instruction*, 4> > InstsToRewrite;
  
  for (Value::use_iterator UI = AI->use_begin(), UE = AI->use_end();
       UI != UE; ++UI) {
    User *U = *UI;
    if (LoadInst *LI = dyn_cast<LoadInst>(U)) {
      if (LI->isVolatile())
        return false;
      continue;
    }
    
    if (StoreInst *SI = dyn_cast<StoreInst>(U)) {
      if (SI->getOperand(0) == AI || SI->isVolatile())
        return false;   // Don't allow a store OF the AI, only INTO the AI.
      continue;
    }

    if (SelectInst *SI = dyn_cast<SelectInst>(U)) {
      // If the condition being selected on is a constant, fold the select, yes
      // this does (rarely) happen early on.
      if (ConstantInt *CI = dyn_cast<ConstantInt>(SI->getCondition())) {
        Value *Result = SI->getOperand(1+CI->isZero());
        SI->replaceAllUsesWith(Result);
        SI->eraseFromParent();
        
        // This is very rare and we just scrambled the use list of AI, start
        // over completely.
        return tryToMakeAllocaBePromotable(AI, TD);
      }

      // If it is safe to turn "load (select c, AI, ptr)" into a select of two
      // loads, then we can transform this by rewriting the select.
      if (!isSafeSelectToSpeculate(SI, TD))
        return false;
      
      InstsToRewrite.insert(SI);
      continue;
    }
    
    if (PHINode *PN = dyn_cast<PHINode>(U)) {
      if (PN->use_empty()) {  // Dead PHIs can be stripped.
        InstsToRewrite.insert(PN);
        continue;
      }
      
      // If it is safe to turn "load (phi [AI, ptr, ...])" into a PHI of loads
      // in the pred blocks, then we can transform this by rewriting the PHI.
      if (!isSafePHIToSpeculate(PN, TD))
        return false;
      
      InstsToRewrite.insert(PN);
      continue;
    }
    
    if (BitCastInst *BCI = dyn_cast<BitCastInst>(U)) {
      if (onlyUsedByLifetimeMarkers(BCI)) {
        InstsToRewrite.insert(BCI);
        continue;
      }
    }
    
    return false;
  }

  // If there are no instructions to rewrite, then all uses are load/stores and
  // we're done!
  if (InstsToRewrite.empty())
    return true;
  
  // If we have instructions that need to be rewritten for this to be promotable
  // take care of it now.
  for (unsigned i = 0, e = InstsToRewrite.size(); i != e; ++i) {
    if (BitCastInst *BCI = dyn_cast<BitCastInst>(InstsToRewrite[i])) {
      // This could only be a bitcast used by nothing but lifetime intrinsics.
      for (BitCastInst::use_iterator I = BCI->use_begin(), E = BCI->use_end();
           I != E;) {
        Use &U = I.getUse();
        ++I;
        cast<Instruction>(U.getUser())->eraseFromParent();
      }
      BCI->eraseFromParent();
      continue;
    }

    if (SelectInst *SI = dyn_cast<SelectInst>(InstsToRewrite[i])) {
      // Selects in InstsToRewrite only have load uses.  Rewrite each as two
      // loads with a new select.
      while (!SI->use_empty()) {
        LoadInst *LI = cast<LoadInst>(SI->use_back());
      
        IRBuilder<> Builder(LI);
        LoadInst *TrueLoad = 
          Builder.CreateLoad(SI->getTrueValue(), LI->getName()+".t");
        LoadInst *FalseLoad = 
          Builder.CreateLoad(SI->getFalseValue(), LI->getName()+".f");
        
        // Transfer alignment and TBAA info if present.
        TrueLoad->setAlignment(LI->getAlignment());
        FalseLoad->setAlignment(LI->getAlignment());
        if (MDNode *Tag = LI->getMetadata(LLVMContext::MD_tbaa)) {
          TrueLoad->setMetadata(LLVMContext::MD_tbaa, Tag);
          FalseLoad->setMetadata(LLVMContext::MD_tbaa, Tag);
        }
        
        Value *V = Builder.CreateSelect(SI->getCondition(), TrueLoad, FalseLoad);
        V->takeName(LI);
        LI->replaceAllUsesWith(V);
        LI->eraseFromParent();
      }
    
      // Now that all the loads are gone, the select is gone too.
      SI->eraseFromParent();
      continue;
    }
    
    // Otherwise, we have a PHI node which allows us to push the loads into the
    // predecessors.
    PHINode *PN = cast<PHINode>(InstsToRewrite[i]);
    if (PN->use_empty()) {
      PN->eraseFromParent();
      continue;
    }
    
    Type *LoadTy = cast<PointerType>(PN->getType())->getElementType();
    PHINode *NewPN = PHINode::Create(LoadTy, PN->getNumIncomingValues(),
                                     PN->getName()+".ld", PN);

    // Get the TBAA tag and alignment to use from one of the loads.  It doesn't
    // matter which one we get and if any differ, it doesn't matter.
    LoadInst *SomeLoad = cast<LoadInst>(PN->use_back());
    MDNode *TBAATag = SomeLoad->getMetadata(LLVMContext::MD_tbaa);
    unsigned Align = SomeLoad->getAlignment();
    
    // Rewrite all loads of the PN to use the new PHI.
    while (!PN->use_empty()) {
      LoadInst *LI = cast<LoadInst>(PN->use_back());
      LI->replaceAllUsesWith(NewPN);
      LI->eraseFromParent();
    }
    
    // Inject loads into all of the pred blocks.  Keep track of which blocks we
    // insert them into in case we have multiple edges from the same block.
    DenseMap<BasicBlock*, LoadInst*> InsertedLoads;
    
    for (unsigned i = 0, e = PN->getNumIncomingValues(); i != e; ++i) {
      BasicBlock *Pred = PN->getIncomingBlock(i);
      LoadInst *&Load = InsertedLoads[Pred];
      if (Load == 0) {
        Load = new LoadInst(PN->getIncomingValue(i),
                            PN->getName() + "." + Pred->getName(),
                            Pred->getTerminator());
        Load->setAlignment(Align);
        if (TBAATag) Load->setMetadata(LLVMContext::MD_tbaa, TBAATag);
      }
      
      NewPN->addIncoming(Load, Pred);
    }
    
    PN->eraseFromParent();
  }
    
  ++NumAdjusted;
  return true;
}

bool SROA::performPromotion(Function &F) {
  std::vector<AllocaInst*> Allocas;
  DominatorTree *DT = 0;
  if (HasDomTree)
    DT = &getAnalysis<DominatorTree>();

  BasicBlock &BB = F.getEntryBlock();  // Get the entry node for the function
  DIBuilder DIB(*F.getParent());
  bool Changed = false;
  SmallVector<Instruction*, 64> Insts;
  while (1) {
    Allocas.clear();

    // Find allocas that are safe to promote, by looking at all instructions in
    // the entry node
    for (BasicBlock::iterator I = BB.begin(), E = --BB.end(); I != E; ++I)
      if (AllocaInst *AI = dyn_cast<AllocaInst>(I))       // Is it an alloca?
        if (tryToMakeAllocaBePromotable(AI, TD))
          Allocas.push_back(AI);

    if (Allocas.empty()) break;

    if (HasDomTree)
      PromoteMemToReg(Allocas, *DT);
    else {
      SSAUpdater SSA;
      for (unsigned i = 0, e = Allocas.size(); i != e; ++i) {
        AllocaInst *AI = Allocas[i];
        
        // Build list of instructions to promote.
        for (Value::use_iterator UI = AI->use_begin(), E = AI->use_end();
             UI != E; ++UI)
          Insts.push_back(cast<Instruction>(*UI));
        AllocaPromoter(Insts, SSA, &DIB).run(AI, Insts);
        Insts.clear();
      }
    }
    NumPromoted += Allocas.size();
    Changed = true;
  }

  return Changed;
}


/// ShouldAttemptScalarRepl - Decide if an alloca is a good candidate for
/// SROA.  It must be a struct or array type with a small number of elements.
static bool ShouldAttemptScalarRepl(AllocaInst *AI) {
  Type *T = AI->getAllocatedType();
  // Do not promote any struct into more than 32 separate vars.
  if (StructType *ST = dyn_cast<StructType>(T))
    return ST->getNumElements() <= 32;
  // Arrays are much less likely to be safe for SROA; only consider
  // them if they are very small.
  if (ArrayType *AT = dyn_cast<ArrayType>(T))
    return AT->getNumElements() <= 8;
  return false;
}


// performScalarRepl - This algorithm is a simple worklist driven algorithm,
// which runs on all of the alloca instructions in the function, removing them
// if they are only used by getelementptr instructions.
//
bool SROA::performScalarRepl(Function &F) {
  std::vector<AllocaInst*> WorkList;

  // Scan the entry basic block, adding allocas to the worklist.
  BasicBlock &BB = F.getEntryBlock();
  for (BasicBlock::iterator I = BB.begin(), E = BB.end(); I != E; ++I)
    if (AllocaInst *A = dyn_cast<AllocaInst>(I))
      WorkList.push_back(A);

  // Process the worklist
  bool Changed = false;
  while (!WorkList.empty()) {
    AllocaInst *AI = WorkList.back();
    WorkList.pop_back();

    // Handle dead allocas trivially.  These can be formed by SROA'ing arrays
    // with unused elements.
    if (AI->use_empty()) {
      AI->eraseFromParent();
      Changed = true;
      continue;
    }

    // If this alloca is impossible for us to promote, reject it early.
    if (AI->isArrayAllocation() || !AI->getAllocatedType()->isSized())
      continue;

    // Check to see if this allocation is only modified by a memcpy/memmove from
    // a constant global.  If this is the case, we can change all users to use
    // the constant global instead.  This is commonly produced by the CFE by
    // constructs like "void foo() { int A[] = {1,2,3,4,5,6,7,8,9...}; }" if 'A'
    // is only subsequently read.
    SmallVector<Instruction *, 4> ToDelete;
    if (MemTransferInst *Copy = isOnlyCopiedFromConstantGlobal(AI, ToDelete)) {
      DEBUG(dbgs() << "Found alloca equal to global: " << *AI << '\n');
      DEBUG(dbgs() << "  memcpy = " << *Copy << '\n');
      for (unsigned i = 0, e = ToDelete.size(); i != e; ++i)
        ToDelete[i]->eraseFromParent();
      Constant *TheSrc = cast<Constant>(Copy->getSource());
      AI->replaceAllUsesWith(ConstantExpr::getBitCast(TheSrc, AI->getType()));
      Copy->eraseFromParent();  // Don't mutate the global.
      AI->eraseFromParent();
      ++NumGlobals;
      Changed = true;
      continue;
    }

    // Check to see if we can perform the core SROA transformation.  We cannot
    // transform the allocation instruction if it is an array allocation
    // (allocations OF arrays are ok though), and an allocation of a scalar
    // value cannot be decomposed at all.
    uint64_t AllocaSize = TD->getTypeAllocSize(AI->getAllocatedType());

    // Do not promote [0 x %struct].
    if (AllocaSize == 0) continue;

    // Do not promote any struct whose size is too big.
    if (AllocaSize > SRThreshold) continue;

    // If the alloca looks like a good candidate for scalar replacement, and if
    // all its users can be transformed, then split up the aggregate into its
    // separate elements.
    if (ShouldAttemptScalarRepl(AI) && isSafeAllocaToScalarRepl(AI)) {
      DoScalarReplacement(AI, WorkList);
      Changed = true;
      continue;
    }

    // If we can turn this aggregate value (potentially with casts) into a
    // simple scalar value that can be mem2reg'd into a register value.
    // IsNotTrivial tracks whether this is something that mem2reg could have
    // promoted itself.  If so, we don't want to transform it needlessly.  Note
    // that we can't just check based on the type: the alloca may be of an i32
    // but that has pointer arithmetic to set byte 3 of it or something.
    if (AllocaInst *NewAI =
          ConvertToScalarInfo((unsigned)AllocaSize, *TD).TryConvert(AI)) {
      NewAI->takeName(AI);
      AI->eraseFromParent();
      ++NumConverted;
      Changed = true;
      continue;
    }

    // Otherwise, couldn't process this alloca.
  }

  return Changed;
}

/// DoScalarReplacement - This alloca satisfied the isSafeAllocaToScalarRepl
/// predicate, do SROA now.
void SROA::DoScalarReplacement(AllocaInst *AI,
                               std::vector<AllocaInst*> &WorkList) {
  DEBUG(dbgs() << "Found inst to SROA: " << *AI << '\n');
  SmallVector<AllocaInst*, 32> ElementAllocas;
  if (StructType *ST = dyn_cast<StructType>(AI->getAllocatedType())) {
    ElementAllocas.reserve(ST->getNumContainedTypes());
    for (unsigned i = 0, e = ST->getNumContainedTypes(); i != e; ++i) {
      AllocaInst *NA = new AllocaInst(ST->getContainedType(i), 0,
                                      AI->getAlignment(),
                                      AI->getName() + "." + Twine(i), AI);
      ElementAllocas.push_back(NA);
      WorkList.push_back(NA);  // Add to worklist for recursive processing
    }
  } else {
    ArrayType *AT = cast<ArrayType>(AI->getAllocatedType());
    ElementAllocas.reserve(AT->getNumElements());
    Type *ElTy = AT->getElementType();
    for (unsigned i = 0, e = AT->getNumElements(); i != e; ++i) {
      AllocaInst *NA = new AllocaInst(ElTy, 0, AI->getAlignment(),
                                      AI->getName() + "." + Twine(i), AI);
      ElementAllocas.push_back(NA);
      WorkList.push_back(NA);  // Add to worklist for recursive processing
    }
  }

  // Now that we have created the new alloca instructions, rewrite all the
  // uses of the old alloca.
  RewriteForScalarRepl(AI, AI, 0, ElementAllocas);

  // Now erase any instructions that were made dead while rewriting the alloca.
  DeleteDeadInstructions();
  AI->eraseFromParent();

  ++NumReplaced;
}

/// DeleteDeadInstructions - Erase instructions on the DeadInstrs list,
/// recursively including all their operands that become trivially dead.
void SROA::DeleteDeadInstructions() {
  while (!DeadInsts.empty()) {
    Instruction *I = cast<Instruction>(DeadInsts.pop_back_val());

    for (User::op_iterator OI = I->op_begin(), E = I->op_end(); OI != E; ++OI)
      if (Instruction *U = dyn_cast<Instruction>(*OI)) {
        // Zero out the operand and see if it becomes trivially dead.
        // (But, don't add allocas to the dead instruction list -- they are
        // already on the worklist and will be deleted separately.)
        *OI = 0;
        if (isInstructionTriviallyDead(U) && !isa<AllocaInst>(U))
          DeadInsts.push_back(U);
      }

    I->eraseFromParent();
  }
}

/// isSafeForScalarRepl - Check if instruction I is a safe use with regard to
/// performing scalar replacement of alloca AI.  The results are flagged in
/// the Info parameter.  Offset indicates the position within AI that is
/// referenced by this instruction.
void SROA::isSafeForScalarRepl(Instruction *I, uint64_t Offset,
                               AllocaInfo &Info) {
  for (Value::use_iterator UI = I->use_begin(), E = I->use_end(); UI!=E; ++UI) {
    Instruction *User = cast<Instruction>(*UI);

    if (BitCastInst *BC = dyn_cast<BitCastInst>(User)) {
      isSafeForScalarRepl(BC, Offset, Info);
    } else if (GetElementPtrInst *GEPI = dyn_cast<GetElementPtrInst>(User)) {
      uint64_t GEPOffset = Offset;
      isSafeGEP(GEPI, GEPOffset, Info);
      if (!Info.isUnsafe)
        isSafeForScalarRepl(GEPI, GEPOffset, Info);
    } else if (MemIntrinsic *MI = dyn_cast<MemIntrinsic>(User)) {
      ConstantInt *Length = dyn_cast<ConstantInt>(MI->getLength());
      if (Length == 0)
        return MarkUnsafe(Info, User);
      isSafeMemAccess(Offset, Length->getZExtValue(), 0,
                      UI.getOperandNo() == 0, Info, MI,
                      true /*AllowWholeAccess*/);
    } else if (LoadInst *LI = dyn_cast<LoadInst>(User)) {
      if (LI->isVolatile())
        return MarkUnsafe(Info, User);
      Type *LIType = LI->getType();
      isSafeMemAccess(Offset, TD->getTypeAllocSize(LIType),
                      LIType, false, Info, LI, true /*AllowWholeAccess*/);
      Info.hasALoadOrStore = true;
        
    } else if (StoreInst *SI = dyn_cast<StoreInst>(User)) {
      // Store is ok if storing INTO the pointer, not storing the pointer
      if (SI->isVolatile() || SI->getOperand(0) == I)
        return MarkUnsafe(Info, User);
        
      Type *SIType = SI->getOperand(0)->getType();
      isSafeMemAccess(Offset, TD->getTypeAllocSize(SIType),
                      SIType, true, Info, SI, true /*AllowWholeAccess*/);
      Info.hasALoadOrStore = true;
    } else if (IntrinsicInst *II = dyn_cast<IntrinsicInst>(User)) {
      if (II->getIntrinsicID() != Intrinsic::lifetime_start &&
          II->getIntrinsicID() != Intrinsic::lifetime_end)
        return MarkUnsafe(Info, User);
    } else if (isa<PHINode>(User) || isa<SelectInst>(User)) {
      isSafePHISelectUseForScalarRepl(User, Offset, Info);
    } else {
      return MarkUnsafe(Info, User);
    }
    if (Info.isUnsafe) return;
  }
}
 

/// isSafePHIUseForScalarRepl - If we see a PHI node or select using a pointer
/// derived from the alloca, we can often still split the alloca into elements.
/// This is useful if we have a large alloca where one element is phi'd
/// together somewhere: we can SRoA and promote all the other elements even if
/// we end up not being able to promote this one.
///
/// All we require is that the uses of the PHI do not index into other parts of
/// the alloca.  The most important use case for this is single load and stores
/// that are PHI'd together, which can happen due to code sinking.
void SROA::isSafePHISelectUseForScalarRepl(Instruction *I, uint64_t Offset,
                                           AllocaInfo &Info) {
  // If we've already checked this PHI, don't do it again.
  if (PHINode *PN = dyn_cast<PHINode>(I))
    if (!Info.CheckedPHIs.insert(PN))
      return;
  
  for (Value::use_iterator UI = I->use_begin(), E = I->use_end(); UI!=E; ++UI) {
    Instruction *User = cast<Instruction>(*UI);
    
    if (BitCastInst *BC = dyn_cast<BitCastInst>(User)) {
      isSafePHISelectUseForScalarRepl(BC, Offset, Info);
    } else if (GetElementPtrInst *GEPI = dyn_cast<GetElementPtrInst>(User)) {
      // Only allow "bitcast" GEPs for simplicity.  We could generalize this,
      // but would have to prove that we're staying inside of an element being
      // promoted.
      if (!GEPI->hasAllZeroIndices())
        return MarkUnsafe(Info, User);
      isSafePHISelectUseForScalarRepl(GEPI, Offset, Info);
    } else if (LoadInst *LI = dyn_cast<LoadInst>(User)) {
      if (LI->isVolatile())
        return MarkUnsafe(Info, User);
      Type *LIType = LI->getType();
      isSafeMemAccess(Offset, TD->getTypeAllocSize(LIType),
                      LIType, false, Info, LI, false /*AllowWholeAccess*/);
      Info.hasALoadOrStore = true;
      
    } else if (StoreInst *SI = dyn_cast<StoreInst>(User)) {
      // Store is ok if storing INTO the pointer, not storing the pointer
      if (SI->isVolatile() || SI->getOperand(0) == I)
        return MarkUnsafe(Info, User);
      
      Type *SIType = SI->getOperand(0)->getType();
      isSafeMemAccess(Offset, TD->getTypeAllocSize(SIType),
                      SIType, true, Info, SI, false /*AllowWholeAccess*/);
      Info.hasALoadOrStore = true;
    } else if (isa<PHINode>(User) || isa<SelectInst>(User)) {
      isSafePHISelectUseForScalarRepl(User, Offset, Info);
    } else {
      return MarkUnsafe(Info, User);
    }
    if (Info.isUnsafe) return;
  }
}

/// isSafeGEP - Check if a GEP instruction can be handled for scalar
/// replacement.  It is safe when all the indices are constant, in-bounds
/// references, and when the resulting offset corresponds to an element within
/// the alloca type.  The results are flagged in the Info parameter.  Upon
/// return, Offset is adjusted as specified by the GEP indices.
void SROA::isSafeGEP(GetElementPtrInst *GEPI,
                     uint64_t &Offset, AllocaInfo &Info) {
  gep_type_iterator GEPIt = gep_type_begin(GEPI), E = gep_type_end(GEPI);
  if (GEPIt == E)
    return;

  // Walk through the GEP type indices, checking the types that this indexes
  // into.
  for (; GEPIt != E; ++GEPIt) {
    // Ignore struct elements, no extra checking needed for these.
    if ((*GEPIt)->isStructTy())
      continue;

    ConstantInt *IdxVal = dyn_cast<ConstantInt>(GEPIt.getOperand());
    if (!IdxVal)
      return MarkUnsafe(Info, GEPI);
  }

  // Compute the offset due to this GEP and check if the alloca has a
  // component element at that offset.
  SmallVector<Value*, 8> Indices(GEPI->op_begin() + 1, GEPI->op_end());
  Offset += TD->getIndexedOffset(GEPI->getPointerOperandType(), Indices);
  if (!TypeHasComponent(Info.AI->getAllocatedType(), Offset, 0))
    MarkUnsafe(Info, GEPI);
}

/// isHomogeneousAggregate - Check if type T is a struct or array containing
/// elements of the same type (which is always true for arrays).  If so,
/// return true with NumElts and EltTy set to the number of elements and the
/// element type, respectively.
static bool isHomogeneousAggregate(Type *T, unsigned &NumElts,
                                   Type *&EltTy) {
  if (ArrayType *AT = dyn_cast<ArrayType>(T)) {
    NumElts = AT->getNumElements();
    EltTy = (NumElts == 0 ? 0 : AT->getElementType());
    return true;
  }
  if (StructType *ST = dyn_cast<StructType>(T)) {
    NumElts = ST->getNumContainedTypes();
    EltTy = (NumElts == 0 ? 0 : ST->getContainedType(0));
    for (unsigned n = 1; n < NumElts; ++n) {
      if (ST->getContainedType(n) != EltTy)
        return false;
    }
    return true;
  }
  return false;
}

/// isCompatibleAggregate - Check if T1 and T2 are either the same type or are
/// "homogeneous" aggregates with the same element type and number of elements.
static bool isCompatibleAggregate(Type *T1, Type *T2) {
  if (T1 == T2)
    return true;

  unsigned NumElts1, NumElts2;
  Type *EltTy1, *EltTy2;
  if (isHomogeneousAggregate(T1, NumElts1, EltTy1) &&
      isHomogeneousAggregate(T2, NumElts2, EltTy2) &&
      NumElts1 == NumElts2 &&
      EltTy1 == EltTy2)
    return true;

  return false;
}

/// isSafeMemAccess - Check if a load/store/memcpy operates on the entire AI
/// alloca or has an offset and size that corresponds to a component element
/// within it.  The offset checked here may have been formed from a GEP with a
/// pointer bitcasted to a different type.
///
/// If AllowWholeAccess is true, then this allows uses of the entire alloca as a
/// unit.  If false, it only allows accesses known to be in a single element.
void SROA::isSafeMemAccess(uint64_t Offset, uint64_t MemSize,
                           Type *MemOpType, bool isStore,
                           AllocaInfo &Info, Instruction *TheAccess,
                           bool AllowWholeAccess) {
  // Check if this is a load/store of the entire alloca.
  if (Offset == 0 && AllowWholeAccess &&
      MemSize == TD->getTypeAllocSize(Info.AI->getAllocatedType())) {
    // This can be safe for MemIntrinsics (where MemOpType is 0) and integer
    // loads/stores (which are essentially the same as the MemIntrinsics with
    // regard to copying padding between elements).  But, if an alloca is
    // flagged as both a source and destination of such operations, we'll need
    // to check later for padding between elements.
    if (!MemOpType || MemOpType->isIntegerTy()) {
      if (isStore)
        Info.isMemCpyDst = true;
      else
        Info.isMemCpySrc = true;
      return;
    }
    // This is also safe for references using a type that is compatible with
    // the type of the alloca, so that loads/stores can be rewritten using
    // insertvalue/extractvalue.
    if (isCompatibleAggregate(MemOpType, Info.AI->getAllocatedType())) {
      Info.hasSubelementAccess = true;
      return;
    }
  }
  // Check if the offset/size correspond to a component within the alloca type.
  Type *T = Info.AI->getAllocatedType();
  if (TypeHasComponent(T, Offset, MemSize)) {
    Info.hasSubelementAccess = true;
    return;
  }

  return MarkUnsafe(Info, TheAccess);
}

/// TypeHasComponent - Return true if T has a component type with the
/// specified offset and size.  If Size is zero, do not check the size.
bool SROA::TypeHasComponent(Type *T, uint64_t Offset, uint64_t Size) {
  Type *EltTy;
  uint64_t EltSize;
  if (StructType *ST = dyn_cast<StructType>(T)) {
    const StructLayout *Layout = TD->getStructLayout(ST);
    unsigned EltIdx = Layout->getElementContainingOffset(Offset);
    EltTy = ST->getContainedType(EltIdx);
    EltSize = TD->getTypeAllocSize(EltTy);
    Offset -= Layout->getElementOffset(EltIdx);
  } else if (ArrayType *AT = dyn_cast<ArrayType>(T)) {
    EltTy = AT->getElementType();
    EltSize = TD->getTypeAllocSize(EltTy);
    if (Offset >= AT->getNumElements() * EltSize)
      return false;
    Offset %= EltSize;
  } else {
    return false;
  }
  if (Offset == 0 && (Size == 0 || EltSize == Size))
    return true;
  // Check if the component spans multiple elements.
  if (Offset + Size > EltSize)
    return false;
  return TypeHasComponent(EltTy, Offset, Size);
}

/// RewriteForScalarRepl - Alloca AI is being split into NewElts, so rewrite
/// the instruction I, which references it, to use the separate elements.
/// Offset indicates the position within AI that is referenced by this
/// instruction.
void SROA::RewriteForScalarRepl(Instruction *I, AllocaInst *AI, uint64_t Offset,
                                SmallVector<AllocaInst*, 32> &NewElts) {
  for (Value::use_iterator UI = I->use_begin(), E = I->use_end(); UI!=E;) {
    Use &TheUse = UI.getUse();
    Instruction *User = cast<Instruction>(*UI++);

    if (BitCastInst *BC = dyn_cast<BitCastInst>(User)) {
      RewriteBitCast(BC, AI, Offset, NewElts);
      continue;
    }
    
    if (GetElementPtrInst *GEPI = dyn_cast<GetElementPtrInst>(User)) {
      RewriteGEP(GEPI, AI, Offset, NewElts);
      continue;
    }
    
    if (MemIntrinsic *MI = dyn_cast<MemIntrinsic>(User)) {
      ConstantInt *Length = dyn_cast<ConstantInt>(MI->getLength());
      uint64_t MemSize = Length->getZExtValue();
      if (Offset == 0 &&
          MemSize == TD->getTypeAllocSize(AI->getAllocatedType()))
        RewriteMemIntrinUserOfAlloca(MI, I, AI, NewElts);
      // Otherwise the intrinsic can only touch a single element and the
      // address operand will be updated, so nothing else needs to be done.
      continue;
    }

    if (IntrinsicInst *II = dyn_cast<IntrinsicInst>(User)) {
      if (II->getIntrinsicID() == Intrinsic::lifetime_start ||
          II->getIntrinsicID() == Intrinsic::lifetime_end) {
        RewriteLifetimeIntrinsic(II, AI, Offset, NewElts);
      }
      continue;
    }
    
    if (LoadInst *LI = dyn_cast<LoadInst>(User)) {
      Type *LIType = LI->getType();
      
      if (isCompatibleAggregate(LIType, AI->getAllocatedType())) {
        // Replace:
        //   %res = load { i32, i32 }* %alloc
        // with:
        //   %load.0 = load i32* %alloc.0
        //   %insert.0 insertvalue { i32, i32 } zeroinitializer, i32 %load.0, 0
        //   %load.1 = load i32* %alloc.1
        //   %insert = insertvalue { i32, i32 } %insert.0, i32 %load.1, 1
        // (Also works for arrays instead of structs)
        Value *Insert = UndefValue::get(LIType);
        IRBuilder<> Builder(LI);
        for (unsigned i = 0, e = NewElts.size(); i != e; ++i) {
          Value *Load = Builder.CreateLoad(NewElts[i], "load");
          Insert = Builder.CreateInsertValue(Insert, Load, i, "insert");
        }
        LI->replaceAllUsesWith(Insert);
        DeadInsts.push_back(LI);
      } else if (LIType->isIntegerTy() &&
                 TD->getTypeAllocSize(LIType) ==
                 TD->getTypeAllocSize(AI->getAllocatedType())) {
        // If this is a load of the entire alloca to an integer, rewrite it.
        RewriteLoadUserOfWholeAlloca(LI, AI, NewElts);
      }
      continue;
    }
    
    if (StoreInst *SI = dyn_cast<StoreInst>(User)) {
      Value *Val = SI->getOperand(0);
      Type *SIType = Val->getType();
      if (isCompatibleAggregate(SIType, AI->getAllocatedType())) {
        // Replace:
        //   store { i32, i32 } %val, { i32, i32 }* %alloc
        // with:
        //   %val.0 = extractvalue { i32, i32 } %val, 0
        //   store i32 %val.0, i32* %alloc.0
        //   %val.1 = extractvalue { i32, i32 } %val, 1
        //   store i32 %val.1, i32* %alloc.1
        // (Also works for arrays instead of structs)
        IRBuilder<> Builder(SI);
        for (unsigned i = 0, e = NewElts.size(); i != e; ++i) {
          Value *Extract = Builder.CreateExtractValue(Val, i, Val->getName());
          Builder.CreateStore(Extract, NewElts[i]);
        }
        DeadInsts.push_back(SI);
      } else if (SIType->isIntegerTy() &&
                 TD->getTypeAllocSize(SIType) ==
                 TD->getTypeAllocSize(AI->getAllocatedType())) {
        // If this is a store of the entire alloca from an integer, rewrite it.
        RewriteStoreUserOfWholeAlloca(SI, AI, NewElts);
      }
      continue;
    }
    
    if (isa<SelectInst>(User) || isa<PHINode>(User)) {
      // If we have a PHI user of the alloca itself (as opposed to a GEP or 
      // bitcast) we have to rewrite it.  GEP and bitcast uses will be RAUW'd to
      // the new pointer.
      if (!isa<AllocaInst>(I)) continue;
      
      assert(Offset == 0 && NewElts[0] &&
             "Direct alloca use should have a zero offset");
      
      // If we have a use of the alloca, we know the derived uses will be
      // utilizing just the first element of the scalarized result.  Insert a
      // bitcast of the first alloca before the user as required.
      AllocaInst *NewAI = NewElts[0];
      BitCastInst *BCI = new BitCastInst(NewAI, AI->getType(), "", NewAI);
      NewAI->moveBefore(BCI);
      TheUse = BCI;
      continue;
    }
  }
}

/// RewriteBitCast - Update a bitcast reference to the alloca being replaced
/// and recursively continue updating all of its uses.
void SROA::RewriteBitCast(BitCastInst *BC, AllocaInst *AI, uint64_t Offset,
                          SmallVector<AllocaInst*, 32> &NewElts) {
  RewriteForScalarRepl(BC, AI, Offset, NewElts);
  if (BC->getOperand(0) != AI)
    return;

  // The bitcast references the original alloca.  Replace its uses with
  // references to the first new element alloca.
  Instruction *Val = NewElts[0];
  if (Val->getType() != BC->getDestTy()) {
    Val = new BitCastInst(Val, BC->getDestTy(), "", BC);
    Val->takeName(BC);
  }
  BC->replaceAllUsesWith(Val);
  DeadInsts.push_back(BC);
}

/// FindElementAndOffset - Return the index of the element containing Offset
/// within the specified type, which must be either a struct or an array.
/// Sets T to the type of the element and Offset to the offset within that
/// element.  IdxTy is set to the type of the index result to be used in a
/// GEP instruction.
uint64_t SROA::FindElementAndOffset(Type *&T, uint64_t &Offset,
                                    Type *&IdxTy) {
  uint64_t Idx = 0;
  if (StructType *ST = dyn_cast<StructType>(T)) {
    const StructLayout *Layout = TD->getStructLayout(ST);
    Idx = Layout->getElementContainingOffset(Offset);
    T = ST->getContainedType(Idx);
    Offset -= Layout->getElementOffset(Idx);
    IdxTy = Type::getInt32Ty(T->getContext());
    return Idx;
  }
  ArrayType *AT = cast<ArrayType>(T);
  T = AT->getElementType();
  uint64_t EltSize = TD->getTypeAllocSize(T);
  Idx = Offset / EltSize;
  Offset -= Idx * EltSize;
  IdxTy = Type::getInt64Ty(T->getContext());
  return Idx;
}

/// RewriteGEP - Check if this GEP instruction moves the pointer across
/// elements of the alloca that are being split apart, and if so, rewrite
/// the GEP to be relative to the new element.
void SROA::RewriteGEP(GetElementPtrInst *GEPI, AllocaInst *AI, uint64_t Offset,
                      SmallVector<AllocaInst*, 32> &NewElts) {
  uint64_t OldOffset = Offset;
  SmallVector<Value*, 8> Indices(GEPI->op_begin() + 1, GEPI->op_end());
  Offset += TD->getIndexedOffset(GEPI->getPointerOperandType(), Indices);

  RewriteForScalarRepl(GEPI, AI, Offset, NewElts);

  Type *T = AI->getAllocatedType();
  Type *IdxTy;
  uint64_t OldIdx = FindElementAndOffset(T, OldOffset, IdxTy);
  if (GEPI->getOperand(0) == AI)
    OldIdx = ~0ULL; // Force the GEP to be rewritten.

  T = AI->getAllocatedType();
  uint64_t EltOffset = Offset;
  uint64_t Idx = FindElementAndOffset(T, EltOffset, IdxTy);

  // If this GEP does not move the pointer across elements of the alloca
  // being split, then it does not needs to be rewritten.
  if (Idx == OldIdx)
    return;

  Type *i32Ty = Type::getInt32Ty(AI->getContext());
  SmallVector<Value*, 8> NewArgs;
  NewArgs.push_back(Constant::getNullValue(i32Ty));
  while (EltOffset != 0) {
    uint64_t EltIdx = FindElementAndOffset(T, EltOffset, IdxTy);
    NewArgs.push_back(ConstantInt::get(IdxTy, EltIdx));
  }
  Instruction *Val = NewElts[Idx];
  if (NewArgs.size() > 1) {
    Val = GetElementPtrInst::CreateInBounds(Val, NewArgs, "", GEPI);
    Val->takeName(GEPI);
  }
  if (Val->getType() != GEPI->getType())
    Val = new BitCastInst(Val, GEPI->getType(), Val->getName(), GEPI);
  GEPI->replaceAllUsesWith(Val);
  DeadInsts.push_back(GEPI);
}

/// RewriteLifetimeIntrinsic - II is a lifetime.start/lifetime.end. Rewrite it
/// to mark the lifetime of the scalarized memory.
void SROA::RewriteLifetimeIntrinsic(IntrinsicInst *II, AllocaInst *AI,
                                    uint64_t Offset,
                                    SmallVector<AllocaInst*, 32> &NewElts) {
  ConstantInt *OldSize = cast<ConstantInt>(II->getArgOperand(0));
  // Put matching lifetime markers on everything from Offset up to
  // Offset+OldSize.
  Type *AIType = AI->getAllocatedType();
  uint64_t NewOffset = Offset;
  Type *IdxTy;
  uint64_t Idx = FindElementAndOffset(AIType, NewOffset, IdxTy);

  IRBuilder<> Builder(II);
  uint64_t Size = OldSize->getLimitedValue();

  if (NewOffset) {
    // Splice the first element and index 'NewOffset' bytes in.  SROA will
    // split the alloca again later.
    Value *V = Builder.CreateBitCast(NewElts[Idx], Builder.getInt8PtrTy());
    V = Builder.CreateGEP(V, Builder.getInt64(NewOffset));

    IdxTy = NewElts[Idx]->getAllocatedType();
    uint64_t EltSize = TD->getTypeAllocSize(IdxTy) - NewOffset;
    if (EltSize > Size) {
      EltSize = Size;
      Size = 0;
    } else {
      Size -= EltSize;
    }
    if (II->getIntrinsicID() == Intrinsic::lifetime_start)
      Builder.CreateLifetimeStart(V, Builder.getInt64(EltSize));
    else
      Builder.CreateLifetimeEnd(V, Builder.getInt64(EltSize));
    ++Idx;
  }

  for (; Idx != NewElts.size() && Size; ++Idx) {
    IdxTy = NewElts[Idx]->getAllocatedType();
    uint64_t EltSize = TD->getTypeAllocSize(IdxTy);
    if (EltSize > Size) {
      EltSize = Size;
      Size = 0;
    } else {
      Size -= EltSize;
    }
    if (II->getIntrinsicID() == Intrinsic::lifetime_start)
      Builder.CreateLifetimeStart(NewElts[Idx],
                                  Builder.getInt64(EltSize));
    else
      Builder.CreateLifetimeEnd(NewElts[Idx],
                                Builder.getInt64(EltSize));
  }
  DeadInsts.push_back(II);
}

/// RewriteMemIntrinUserOfAlloca - MI is a memcpy/memset/memmove from or to AI.
/// Rewrite it to copy or set the elements of the scalarized memory.
void SROA::RewriteMemIntrinUserOfAlloca(MemIntrinsic *MI, Instruction *Inst,
                                        AllocaInst *AI,
                                        SmallVector<AllocaInst*, 32> &NewElts) {
  // If this is a memcpy/memmove, construct the other pointer as the
  // appropriate type.  The "Other" pointer is the pointer that goes to memory
  // that doesn't have anything to do with the alloca that we are promoting. For
  // memset, this Value* stays null.
  Value *OtherPtr = 0;
  unsigned MemAlignment = MI->getAlignment();
  if (MemTransferInst *MTI = dyn_cast<MemTransferInst>(MI)) { // memmove/memcopy
    if (Inst == MTI->getRawDest())
      OtherPtr = MTI->getRawSource();
    else {
      assert(Inst == MTI->getRawSource());
      OtherPtr = MTI->getRawDest();
    }
  }

  // If there is an other pointer, we want to convert it to the same pointer
  // type as AI has, so we can GEP through it safely.
  if (OtherPtr) {
    unsigned AddrSpace =
      cast<PointerType>(OtherPtr->getType())->getAddressSpace();

    // Remove bitcasts and all-zero GEPs from OtherPtr.  This is an
    // optimization, but it's also required to detect the corner case where
    // both pointer operands are referencing the same memory, and where
    // OtherPtr may be a bitcast or GEP that currently being rewritten.  (This
    // function is only called for mem intrinsics that access the whole
    // aggregate, so non-zero GEPs are not an issue here.)
    OtherPtr = OtherPtr->stripPointerCasts();

    // Copying the alloca to itself is a no-op: just delete it.
    if (OtherPtr == AI || OtherPtr == NewElts[0]) {
      // This code will run twice for a no-op memcpy -- once for each operand.
      // Put only one reference to MI on the DeadInsts list.
      for (SmallVector<Value*, 32>::const_iterator I = DeadInsts.begin(),
             E = DeadInsts.end(); I != E; ++I)
        if (*I == MI) return;
      DeadInsts.push_back(MI);
      return;
    }

    // If the pointer is not the right type, insert a bitcast to the right
    // type.
    Type *NewTy =
      PointerType::get(AI->getType()->getElementType(), AddrSpace);

    if (OtherPtr->getType() != NewTy)
      OtherPtr = new BitCastInst(OtherPtr, NewTy, OtherPtr->getName(), MI);
  }

  // Process each element of the aggregate.
  bool SROADest = MI->getRawDest() == Inst;

  Constant *Zero = Constant::getNullValue(Type::getInt32Ty(MI->getContext()));

  for (unsigned i = 0, e = NewElts.size(); i != e; ++i) {
    // If this is a memcpy/memmove, emit a GEP of the other element address.
    Value *OtherElt = 0;
    unsigned OtherEltAlign = MemAlignment;

    if (OtherPtr) {
      Value *Idx[2] = { Zero,
                      ConstantInt::get(Type::getInt32Ty(MI->getContext()), i) };
      OtherElt = GetElementPtrInst::CreateInBounds(OtherPtr, Idx,
                                              OtherPtr->getName()+"."+Twine(i),
                                                   MI);
      uint64_t EltOffset;
      PointerType *OtherPtrTy = cast<PointerType>(OtherPtr->getType());
      Type *OtherTy = OtherPtrTy->getElementType();
      if (StructType *ST = dyn_cast<StructType>(OtherTy)) {
        EltOffset = TD->getStructLayout(ST)->getElementOffset(i);
      } else {
        Type *EltTy = cast<SequentialType>(OtherTy)->getElementType();
        EltOffset = TD->getTypeAllocSize(EltTy)*i;
      }

      // The alignment of the other pointer is the guaranteed alignment of the
      // element, which is affected by both the known alignment of the whole
      // mem intrinsic and the alignment of the element.  If the alignment of
      // the memcpy (f.e.) is 32 but the element is at a 4-byte offset, then the
      // known alignment is just 4 bytes.
      OtherEltAlign = (unsigned)MinAlign(OtherEltAlign, EltOffset);
    }

    Value *EltPtr = NewElts[i];
    Type *EltTy = cast<PointerType>(EltPtr->getType())->getElementType();

    // If we got down to a scalar, insert a load or store as appropriate.
    if (EltTy->isSingleValueType()) {
      if (isa<MemTransferInst>(MI)) {
        if (SROADest) {
          // From Other to Alloca.
          Value *Elt = new LoadInst(OtherElt, "tmp", false, OtherEltAlign, MI);
          new StoreInst(Elt, EltPtr, MI);
        } else {
          // From Alloca to Other.
          Value *Elt = new LoadInst(EltPtr, "tmp", MI);
          new StoreInst(Elt, OtherElt, false, OtherEltAlign, MI);
        }
        continue;
      }
      assert(isa<MemSetInst>(MI));

      // If the stored element is zero (common case), just store a null
      // constant.
      Constant *StoreVal;
      if (ConstantInt *CI = dyn_cast<ConstantInt>(MI->getArgOperand(1))) {
        if (CI->isZero()) {
          StoreVal = Constant::getNullValue(EltTy);  // 0.0, null, 0, <0,0>
        } else {
          // If EltTy is a vector type, get the element type.
          Type *ValTy = EltTy->getScalarType();

          // Construct an integer with the right value.
          unsigned EltSize = TD->getTypeSizeInBits(ValTy);
          APInt OneVal(EltSize, CI->getZExtValue());
          APInt TotalVal(OneVal);
          // Set each byte.
          for (unsigned i = 0; 8*i < EltSize; ++i) {
            TotalVal = TotalVal.shl(8);
            TotalVal |= OneVal;
          }

          // Convert the integer value to the appropriate type.
          StoreVal = ConstantInt::get(CI->getContext(), TotalVal);
          if (ValTy->isPointerTy())
            StoreVal = ConstantExpr::getIntToPtr(StoreVal, ValTy);
          else if (ValTy->isFloatingPointTy())
            StoreVal = ConstantExpr::getBitCast(StoreVal, ValTy);
          assert(StoreVal->getType() == ValTy && "Type mismatch!");

          // If the requested value was a vector constant, create it.
          if (EltTy != ValTy) {
            unsigned NumElts = cast<VectorType>(ValTy)->getNumElements();
            SmallVector<Constant*, 16> Elts(NumElts, StoreVal);
            StoreVal = ConstantVector::get(Elts);
          }
        }
        new StoreInst(StoreVal, EltPtr, MI);
        continue;
      }
      // Otherwise, if we're storing a byte variable, use a memset call for
      // this element.
    }

    unsigned EltSize = TD->getTypeAllocSize(EltTy);

    IRBuilder<> Builder(MI);

    // Finally, insert the meminst for this element.
    if (isa<MemSetInst>(MI)) {
      Builder.CreateMemSet(EltPtr, MI->getArgOperand(1), EltSize,
                           MI->isVolatile());
    } else {
      assert(isa<MemTransferInst>(MI));
      Value *Dst = SROADest ? EltPtr : OtherElt;  // Dest ptr
      Value *Src = SROADest ? OtherElt : EltPtr;  // Src ptr

      if (isa<MemCpyInst>(MI))
        Builder.CreateMemCpy(Dst, Src, EltSize, OtherEltAlign,MI->isVolatile());
      else
        Builder.CreateMemMove(Dst, Src, EltSize,OtherEltAlign,MI->isVolatile());
    }
  }
  DeadInsts.push_back(MI);
}

/// RewriteStoreUserOfWholeAlloca - We found a store of an integer that
/// overwrites the entire allocation.  Extract out the pieces of the stored
/// integer and store them individually.
void SROA::RewriteStoreUserOfWholeAlloca(StoreInst *SI, AllocaInst *AI,
                                         SmallVector<AllocaInst*, 32> &NewElts){
  // Extract each element out of the integer according to its structure offset
  // and store the element value to the individual alloca.
  Value *SrcVal = SI->getOperand(0);
  Type *AllocaEltTy = AI->getAllocatedType();
  uint64_t AllocaSizeBits = TD->getTypeAllocSizeInBits(AllocaEltTy);

  IRBuilder<> Builder(SI);
  
  // Handle tail padding by extending the operand
  if (TD->getTypeSizeInBits(SrcVal->getType()) != AllocaSizeBits)
    SrcVal = Builder.CreateZExt(SrcVal,
                            IntegerType::get(SI->getContext(), AllocaSizeBits));

  DEBUG(dbgs() << "PROMOTING STORE TO WHOLE ALLOCA: " << *AI << '\n' << *SI
               << '\n');

  // There are two forms here: AI could be an array or struct.  Both cases
  // have different ways to compute the element offset.
  if (StructType *EltSTy = dyn_cast<StructType>(AllocaEltTy)) {
    const StructLayout *Layout = TD->getStructLayout(EltSTy);

    for (unsigned i = 0, e = NewElts.size(); i != e; ++i) {
      // Get the number of bits to shift SrcVal to get the value.
      Type *FieldTy = EltSTy->getElementType(i);
      uint64_t Shift = Layout->getElementOffsetInBits(i);

      if (TD->isBigEndian())
        Shift = AllocaSizeBits-Shift-TD->getTypeAllocSizeInBits(FieldTy);

      Value *EltVal = SrcVal;
      if (Shift) {
        Value *ShiftVal = ConstantInt::get(EltVal->getType(), Shift);
        EltVal = Builder.CreateLShr(EltVal, ShiftVal, "sroa.store.elt");
      }

      // Truncate down to an integer of the right size.
      uint64_t FieldSizeBits = TD->getTypeSizeInBits(FieldTy);

      // Ignore zero sized fields like {}, they obviously contain no data.
      if (FieldSizeBits == 0) continue;

      if (FieldSizeBits != AllocaSizeBits)
        EltVal = Builder.CreateTrunc(EltVal,
                             IntegerType::get(SI->getContext(), FieldSizeBits));
      Value *DestField = NewElts[i];
      if (EltVal->getType() == FieldTy) {
        // Storing to an integer field of this size, just do it.
      } else if (FieldTy->isFloatingPointTy() || FieldTy->isVectorTy()) {
        // Bitcast to the right element type (for fp/vector values).
        EltVal = Builder.CreateBitCast(EltVal, FieldTy);
      } else {
        // Otherwise, bitcast the dest pointer (for aggregates).
        DestField = Builder.CreateBitCast(DestField,
                                     PointerType::getUnqual(EltVal->getType()));
      }
      new StoreInst(EltVal, DestField, SI);
    }

  } else {
    ArrayType *ATy = cast<ArrayType>(AllocaEltTy);
    Type *ArrayEltTy = ATy->getElementType();
    uint64_t ElementOffset = TD->getTypeAllocSizeInBits(ArrayEltTy);
    uint64_t ElementSizeBits = TD->getTypeSizeInBits(ArrayEltTy);

    uint64_t Shift;

    if (TD->isBigEndian())
      Shift = AllocaSizeBits-ElementOffset;
    else
      Shift = 0;

    for (unsigned i = 0, e = NewElts.size(); i != e; ++i) {
      // Ignore zero sized fields like {}, they obviously contain no data.
      if (ElementSizeBits == 0) continue;

      Value *EltVal = SrcVal;
      if (Shift) {
        Value *ShiftVal = ConstantInt::get(EltVal->getType(), Shift);
        EltVal = Builder.CreateLShr(EltVal, ShiftVal, "sroa.store.elt");
      }

      // Truncate down to an integer of the right size.
      if (ElementSizeBits != AllocaSizeBits)
        EltVal = Builder.CreateTrunc(EltVal,
                                     IntegerType::get(SI->getContext(),
                                                      ElementSizeBits));
      Value *DestField = NewElts[i];
      if (EltVal->getType() == ArrayEltTy) {
        // Storing to an integer field of this size, just do it.
      } else if (ArrayEltTy->isFloatingPointTy() ||
                 ArrayEltTy->isVectorTy()) {
        // Bitcast to the right element type (for fp/vector values).
        EltVal = Builder.CreateBitCast(EltVal, ArrayEltTy);
      } else {
        // Otherwise, bitcast the dest pointer (for aggregates).
        DestField = Builder.CreateBitCast(DestField,
                                     PointerType::getUnqual(EltVal->getType()));
      }
      new StoreInst(EltVal, DestField, SI);

      if (TD->isBigEndian())
        Shift -= ElementOffset;
      else
        Shift += ElementOffset;
    }
  }

  DeadInsts.push_back(SI);
}

/// RewriteLoadUserOfWholeAlloca - We found a load of the entire allocation to
/// an integer.  Load the individual pieces to form the aggregate value.
void SROA::RewriteLoadUserOfWholeAlloca(LoadInst *LI, AllocaInst *AI,
                                        SmallVector<AllocaInst*, 32> &NewElts) {
  // Extract each element out of the NewElts according to its structure offset
  // and form the result value.
  Type *AllocaEltTy = AI->getAllocatedType();
  uint64_t AllocaSizeBits = TD->getTypeAllocSizeInBits(AllocaEltTy);

  DEBUG(dbgs() << "PROMOTING LOAD OF WHOLE ALLOCA: " << *AI << '\n' << *LI
               << '\n');

  // There are two forms here: AI could be an array or struct.  Both cases
  // have different ways to compute the element offset.
  const StructLayout *Layout = 0;
  uint64_t ArrayEltBitOffset = 0;
  if (StructType *EltSTy = dyn_cast<StructType>(AllocaEltTy)) {
    Layout = TD->getStructLayout(EltSTy);
  } else {
    Type *ArrayEltTy = cast<ArrayType>(AllocaEltTy)->getElementType();
    ArrayEltBitOffset = TD->getTypeAllocSizeInBits(ArrayEltTy);
  }

  Value *ResultVal =
    Constant::getNullValue(IntegerType::get(LI->getContext(), AllocaSizeBits));

  for (unsigned i = 0, e = NewElts.size(); i != e; ++i) {
    // Load the value from the alloca.  If the NewElt is an aggregate, cast
    // the pointer to an integer of the same size before doing the load.
    Value *SrcField = NewElts[i];
    Type *FieldTy =
      cast<PointerType>(SrcField->getType())->getElementType();
    uint64_t FieldSizeBits = TD->getTypeSizeInBits(FieldTy);

    // Ignore zero sized fields like {}, they obviously contain no data.
    if (FieldSizeBits == 0) continue;

    IntegerType *FieldIntTy = IntegerType::get(LI->getContext(),
                                                     FieldSizeBits);
    if (!FieldTy->isIntegerTy() && !FieldTy->isFloatingPointTy() &&
        !FieldTy->isVectorTy())
      SrcField = new BitCastInst(SrcField,
                                 PointerType::getUnqual(FieldIntTy),
                                 "", LI);
    SrcField = new LoadInst(SrcField, "sroa.load.elt", LI);

    // If SrcField is a fp or vector of the right size but that isn't an
    // integer type, bitcast to an integer so we can shift it.
    if (SrcField->getType() != FieldIntTy)
      SrcField = new BitCastInst(SrcField, FieldIntTy, "", LI);

    // Zero extend the field to be the same size as the final alloca so that
    // we can shift and insert it.
    if (SrcField->getType() != ResultVal->getType())
      SrcField = new ZExtInst(SrcField, ResultVal->getType(), "", LI);

    // Determine the number of bits to shift SrcField.
    uint64_t Shift;
    if (Layout) // Struct case.
      Shift = Layout->getElementOffsetInBits(i);
    else  // Array case.
      Shift = i*ArrayEltBitOffset;

    if (TD->isBigEndian())
      Shift = AllocaSizeBits-Shift-FieldIntTy->getBitWidth();

    if (Shift) {
      Value *ShiftVal = ConstantInt::get(SrcField->getType(), Shift);
      SrcField = BinaryOperator::CreateShl(SrcField, ShiftVal, "", LI);
    }

    // Don't create an 'or x, 0' on the first iteration.
    if (!isa<Constant>(ResultVal) ||
        !cast<Constant>(ResultVal)->isNullValue())
      ResultVal = BinaryOperator::CreateOr(SrcField, ResultVal, "", LI);
    else
      ResultVal = SrcField;
  }

  // Handle tail padding by truncating the result
  if (TD->getTypeSizeInBits(LI->getType()) != AllocaSizeBits)
    ResultVal = new TruncInst(ResultVal, LI->getType(), "", LI);

  LI->replaceAllUsesWith(ResultVal);
  DeadInsts.push_back(LI);
}

/// HasPadding - Return true if the specified type has any structure or
/// alignment padding in between the elements that would be split apart
/// by SROA; return false otherwise.
static bool HasPadding(Type *Ty, const TargetData &TD) {
  if (ArrayType *ATy = dyn_cast<ArrayType>(Ty)) {
    Ty = ATy->getElementType();
    return TD.getTypeSizeInBits(Ty) != TD.getTypeAllocSizeInBits(Ty);
  }

  // SROA currently handles only Arrays and Structs.
  StructType *STy = cast<StructType>(Ty);
  const StructLayout *SL = TD.getStructLayout(STy);
  unsigned PrevFieldBitOffset = 0;
  for (unsigned i = 0, e = STy->getNumElements(); i != e; ++i) {
    unsigned FieldBitOffset = SL->getElementOffsetInBits(i);

    // Check to see if there is any padding between this element and the
    // previous one.
    if (i) {
      unsigned PrevFieldEnd =
        PrevFieldBitOffset+TD.getTypeSizeInBits(STy->getElementType(i-1));
      if (PrevFieldEnd < FieldBitOffset)
        return true;
    }
    PrevFieldBitOffset = FieldBitOffset;
  }
  // Check for tail padding.
  if (unsigned EltCount = STy->getNumElements()) {
    unsigned PrevFieldEnd = PrevFieldBitOffset +
      TD.getTypeSizeInBits(STy->getElementType(EltCount-1));
    if (PrevFieldEnd < SL->getSizeInBits())
      return true;
  }
  return false;
}

/// isSafeStructAllocaToScalarRepl - Check to see if the specified allocation of
/// an aggregate can be broken down into elements.  Return 0 if not, 3 if safe,
/// or 1 if safe after canonicalization has been performed.
bool SROA::isSafeAllocaToScalarRepl(AllocaInst *AI) {
  // Loop over the use list of the alloca.  We can only transform it if all of
  // the users are safe to transform.
  AllocaInfo Info(AI);

  isSafeForScalarRepl(AI, 0, Info);
  if (Info.isUnsafe) {
    DEBUG(dbgs() << "Cannot transform: " << *AI << '\n');
    return false;
  }

  // Okay, we know all the users are promotable.  If the aggregate is a memcpy
  // source and destination, we have to be careful.  In particular, the memcpy
  // could be moving around elements that live in structure padding of the LLVM
  // types, but may actually be used.  In these cases, we refuse to promote the
  // struct.
  if (Info.isMemCpySrc && Info.isMemCpyDst &&
      HasPadding(AI->getAllocatedType(), *TD))
    return false;

  // If the alloca never has an access to just *part* of it, but is accessed
  // via loads and stores, then we should use ConvertToScalarInfo to promote
  // the alloca instead of promoting each piece at a time and inserting fission
  // and fusion code.
  if (!Info.hasSubelementAccess && Info.hasALoadOrStore) {
    // If the struct/array just has one element, use basic SRoA.
    if (StructType *ST = dyn_cast<StructType>(AI->getAllocatedType())) {
      if (ST->getNumElements() > 1) return false;
    } else {
      if (cast<ArrayType>(AI->getAllocatedType())->getNumElements() > 1)
        return false;
    }
  }
  
  return true;
}



/// PointsToConstantGlobal - Return true if V (possibly indirectly) points to
/// some part of a constant global variable.  This intentionally only accepts
/// constant expressions because we don't can't rewrite arbitrary instructions.
static bool PointsToConstantGlobal(Value *V) {
  if (GlobalVariable *GV = dyn_cast<GlobalVariable>(V))
    return GV->isConstant();
  if (ConstantExpr *CE = dyn_cast<ConstantExpr>(V))
    if (CE->getOpcode() == Instruction::BitCast ||
        CE->getOpcode() == Instruction::GetElementPtr)
      return PointsToConstantGlobal(CE->getOperand(0));
  return false;
}

/// isOnlyCopiedFromConstantGlobal - Recursively walk the uses of a (derived)
/// pointer to an alloca.  Ignore any reads of the pointer, return false if we
/// see any stores or other unknown uses.  If we see pointer arithmetic, keep
/// track of whether it moves the pointer (with isOffset) but otherwise traverse
/// the uses.  If we see a memcpy/memmove that targets an unoffseted pointer to
/// the alloca, and if the source pointer is a pointer to a constant global, we
/// can optimize this.
static bool
isOnlyCopiedFromConstantGlobal(Value *V, MemTransferInst *&TheCopy,
                               bool isOffset,
                               SmallVector<Instruction *, 4> &LifetimeMarkers) {
  // We track lifetime intrinsics as we encounter them.  If we decide to go
  // ahead and replace the value with the global, this lets the caller quickly
  // eliminate the markers.

  for (Value::use_iterator UI = V->use_begin(), E = V->use_end(); UI!=E; ++UI) {
    User *U = cast<Instruction>(*UI);

    if (LoadInst *LI = dyn_cast<LoadInst>(U)) {
      // Ignore non-volatile loads, they are always ok.
      if (LI->isVolatile()) return false;
      continue;
    }

    if (BitCastInst *BCI = dyn_cast<BitCastInst>(U)) {
      // If uses of the bitcast are ok, we are ok.
      if (!isOnlyCopiedFromConstantGlobal(BCI, TheCopy, isOffset,
                                          LifetimeMarkers))
        return false;
      continue;
    }
    if (GetElementPtrInst *GEP = dyn_cast<GetElementPtrInst>(U)) {
      // If the GEP has all zero indices, it doesn't offset the pointer.  If it
      // doesn't, it does.
      if (!isOnlyCopiedFromConstantGlobal(GEP, TheCopy,
                                          isOffset || !GEP->hasAllZeroIndices(),
                                          LifetimeMarkers))
        return false;
      continue;
    }

    if (CallSite CS = U) {
      // If this is the function being called then we treat it like a load and
      // ignore it.
      if (CS.isCallee(UI))
        continue;

      // If this is a readonly/readnone call site, then we know it is just a
      // load (but one that potentially returns the value itself), so we can
      // ignore it if we know that the value isn't captured.
      unsigned ArgNo = CS.getArgumentNo(UI);
      if (CS.onlyReadsMemory() &&
          (CS.getInstruction()->use_empty() ||
           CS.paramHasAttr(ArgNo+1, Attribute::NoCapture)))
        continue;

      // If this is being passed as a byval argument, the caller is making a
      // copy, so it is only a read of the alloca.
      if (CS.paramHasAttr(ArgNo+1, Attribute::ByVal))
        continue;
    }

    // Lifetime intrinsics can be handled by the caller.
    if (IntrinsicInst *II = dyn_cast<IntrinsicInst>(U)) {
      if (II->getIntrinsicID() == Intrinsic::lifetime_start ||
          II->getIntrinsicID() == Intrinsic::lifetime_end) {
        assert(II->use_empty() && "Lifetime markers have no result to use!");
        LifetimeMarkers.push_back(II);
        continue;
      }
    }

    // If this is isn't our memcpy/memmove, reject it as something we can't
    // handle.
    MemTransferInst *MI = dyn_cast<MemTransferInst>(U);
    if (MI == 0)
      return false;

    // If the transfer is using the alloca as a source of the transfer, then
    // ignore it since it is a load (unless the transfer is volatile).
    if (UI.getOperandNo() == 1) {
      if (MI->isVolatile()) return false;
      continue;
    }

    // If we already have seen a copy, reject the second one.
    if (TheCopy) return false;

    // If the pointer has been offset from the start of the alloca, we can't
    // safely handle this.
    if (isOffset) return false;

    // If the memintrinsic isn't using the alloca as the dest, reject it.
    if (UI.getOperandNo() != 0) return false;

    // If the source of the memcpy/move is not a constant global, reject it.
    if (!PointsToConstantGlobal(MI->getSource()))
      return false;

    // Otherwise, the transform is safe.  Remember the copy instruction.
    TheCopy = MI;
  }
  return true;
}

/// isOnlyCopiedFromConstantGlobal - Return true if the specified alloca is only
/// modified by a copy from a constant global.  If we can prove this, we can
/// replace any uses of the alloca with uses of the global directly.
MemTransferInst *
SROA::isOnlyCopiedFromConstantGlobal(AllocaInst *AI,
                                     SmallVector<Instruction*, 4> &ToDelete) {
  MemTransferInst *TheCopy = 0;
  if (::isOnlyCopiedFromConstantGlobal(AI, TheCopy, false, ToDelete))
    return TheCopy;
  return 0;
}
