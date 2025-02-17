//===-- Core.cpp ----------------------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the common infrastructure (including the C bindings)
// for libLLVMCore.a, which implements the LLVM intermediate representation.
//
//===----------------------------------------------------------------------===//

#include "llvm-c/Core.h"
#include "llvm/Bitcode/ReaderWriter.h"
#include "llvm/Constants.h"
#include "llvm/DerivedTypes.h"
#include "llvm/GlobalVariable.h"
#include "llvm/GlobalAlias.h"
#include "llvm/LLVMContext.h"
#include "llvm/InlineAsm.h"
#include "llvm/IntrinsicInst.h"
#include "llvm/PassManager.h"
#include "llvm/Support/CallSite.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/system_error.h"
#include <cassert>
#include <cstdlib>
#include <cstring>

using namespace llvm;

void llvm::initializeCore(PassRegistry &Registry) {
  initializeDominatorTreePass(Registry);
  initializePrintModulePassPass(Registry);
  initializePrintFunctionPassPass(Registry);
  initializeVerifierPass(Registry);
  initializePreVerifierPass(Registry);
}

void LLVMInitializeCore(LLVMPassRegistryRef R) {
  initializeCore(*unwrap(R));
}

/*===-- Error handling ----------------------------------------------------===*/

void LLVMDisposeMessage(char *Message) {
  free(Message);
}


/*===-- Operations on contexts --------------------------------------------===*/

LLVMContextRef LLVMContextCreate() {
  return wrap(new LLVMContext());
}

LLVMContextRef LLVMGetGlobalContext() {
  return wrap(&getGlobalContext());
}

void LLVMContextDispose(LLVMContextRef C) {
  delete unwrap(C);
}

unsigned LLVMGetMDKindIDInContext(LLVMContextRef C, const char* Name,
                                  unsigned SLen) {
  return unwrap(C)->getMDKindID(StringRef(Name, SLen));
}

unsigned LLVMGetMDKindID(const char* Name, unsigned SLen) {
  return LLVMGetMDKindIDInContext(LLVMGetGlobalContext(), Name, SLen);
}


/*===-- Operations on modules ---------------------------------------------===*/

LLVMModuleRef LLVMModuleCreateWithName(const char *ModuleID) {
  return wrap(new Module(ModuleID, getGlobalContext()));
}

LLVMModuleRef LLVMModuleCreateWithNameInContext(const char *ModuleID, 
                                                LLVMContextRef C) {
  return wrap(new Module(ModuleID, *unwrap(C)));
}

void LLVMDisposeModule(LLVMModuleRef M) {
  delete unwrap(M);
}

/*--.. Data layout .........................................................--*/
const char * LLVMGetDataLayout(LLVMModuleRef M) {
  return unwrap(M)->getDataLayout().c_str();
}

void LLVMSetDataLayout(LLVMModuleRef M, const char *Triple) {
  unwrap(M)->setDataLayout(Triple);
}

/*--.. Target triple .......................................................--*/
const char * LLVMGetTarget(LLVMModuleRef M) {
  return unwrap(M)->getTargetTriple().c_str();
}

void LLVMSetTarget(LLVMModuleRef M, const char *Triple) {
  unwrap(M)->setTargetTriple(Triple);
}

void LLVMDumpModule(LLVMModuleRef M) {
  unwrap(M)->dump();
}

/*--.. Operations on inline assembler ......................................--*/
void LLVMSetModuleInlineAsm(LLVMModuleRef M, const char *Asm) {
  unwrap(M)->setModuleInlineAsm(StringRef(Asm));
}


/*--.. Operations on module contexts ......................................--*/
LLVMContextRef LLVMGetModuleContext(LLVMModuleRef M) {
  return wrap(&unwrap(M)->getContext());
}


/*===-- Operations on types -----------------------------------------------===*/

/*--.. Operations on all types (mostly) ....................................--*/

LLVMTypeKind LLVMGetTypeKind(LLVMTypeRef Ty) {
  switch (unwrap(Ty)->getTypeID()) {
  default:
    assert(false && "Unhandled TypeID.");
  case Type::VoidTyID:
    return LLVMVoidTypeKind;
  case Type::FloatTyID:
    return LLVMFloatTypeKind;
  case Type::DoubleTyID:
    return LLVMDoubleTypeKind;
  case Type::X86_FP80TyID:
    return LLVMX86_FP80TypeKind;
  case Type::FP128TyID:
    return LLVMFP128TypeKind;
  case Type::PPC_FP128TyID:
    return LLVMPPC_FP128TypeKind;
  case Type::LabelTyID:
    return LLVMLabelTypeKind;
  case Type::MetadataTyID:
    return LLVMMetadataTypeKind;
  case Type::IntegerTyID:
    return LLVMIntegerTypeKind;
  case Type::FunctionTyID:
    return LLVMFunctionTypeKind;
  case Type::StructTyID:
    return LLVMStructTypeKind;
  case Type::ArrayTyID:
    return LLVMArrayTypeKind;
  case Type::PointerTyID:
    return LLVMPointerTypeKind;
  case Type::VectorTyID:
    return LLVMVectorTypeKind;
  case Type::X86_MMXTyID:
    return LLVMX86_MMXTypeKind;
  }
}

LLVMContextRef LLVMGetTypeContext(LLVMTypeRef Ty) {
  return wrap(&unwrap(Ty)->getContext());
}

/*--.. Operations on integer types .........................................--*/

LLVMTypeRef LLVMInt1TypeInContext(LLVMContextRef C)  {
  return (LLVMTypeRef) Type::getInt1Ty(*unwrap(C));
}
LLVMTypeRef LLVMInt8TypeInContext(LLVMContextRef C)  {
  return (LLVMTypeRef) Type::getInt8Ty(*unwrap(C));
}
LLVMTypeRef LLVMInt16TypeInContext(LLVMContextRef C) {
  return (LLVMTypeRef) Type::getInt16Ty(*unwrap(C));
}
LLVMTypeRef LLVMInt32TypeInContext(LLVMContextRef C) {
  return (LLVMTypeRef) Type::getInt32Ty(*unwrap(C));
}
LLVMTypeRef LLVMInt64TypeInContext(LLVMContextRef C) {
  return (LLVMTypeRef) Type::getInt64Ty(*unwrap(C));
}
LLVMTypeRef LLVMIntTypeInContext(LLVMContextRef C, unsigned NumBits) {
  return wrap(IntegerType::get(*unwrap(C), NumBits));
}

LLVMTypeRef LLVMInt1Type(void)  {
  return LLVMInt1TypeInContext(LLVMGetGlobalContext());
}
LLVMTypeRef LLVMInt8Type(void)  {
  return LLVMInt8TypeInContext(LLVMGetGlobalContext());
}
LLVMTypeRef LLVMInt16Type(void) {
  return LLVMInt16TypeInContext(LLVMGetGlobalContext());
}
LLVMTypeRef LLVMInt32Type(void) {
  return LLVMInt32TypeInContext(LLVMGetGlobalContext());
}
LLVMTypeRef LLVMInt64Type(void) {
  return LLVMInt64TypeInContext(LLVMGetGlobalContext());
}
LLVMTypeRef LLVMIntType(unsigned NumBits) {
  return LLVMIntTypeInContext(LLVMGetGlobalContext(), NumBits);
}

unsigned LLVMGetIntTypeWidth(LLVMTypeRef IntegerTy) {
  return unwrap<IntegerType>(IntegerTy)->getBitWidth();
}

/*--.. Operations on real types ............................................--*/

LLVMTypeRef LLVMFloatTypeInContext(LLVMContextRef C) {
  return (LLVMTypeRef) Type::getFloatTy(*unwrap(C));
}
LLVMTypeRef LLVMDoubleTypeInContext(LLVMContextRef C) {
  return (LLVMTypeRef) Type::getDoubleTy(*unwrap(C));
}
LLVMTypeRef LLVMX86FP80TypeInContext(LLVMContextRef C) {
  return (LLVMTypeRef) Type::getX86_FP80Ty(*unwrap(C));
}
LLVMTypeRef LLVMFP128TypeInContext(LLVMContextRef C) {
  return (LLVMTypeRef) Type::getFP128Ty(*unwrap(C));
}
LLVMTypeRef LLVMPPCFP128TypeInContext(LLVMContextRef C) {
  return (LLVMTypeRef) Type::getPPC_FP128Ty(*unwrap(C));
}
LLVMTypeRef LLVMX86MMXTypeInContext(LLVMContextRef C) {
  return (LLVMTypeRef) Type::getX86_MMXTy(*unwrap(C));
}

LLVMTypeRef LLVMFloatType(void) {
  return LLVMFloatTypeInContext(LLVMGetGlobalContext());
}
LLVMTypeRef LLVMDoubleType(void) {
  return LLVMDoubleTypeInContext(LLVMGetGlobalContext());
}
LLVMTypeRef LLVMX86FP80Type(void) {
  return LLVMX86FP80TypeInContext(LLVMGetGlobalContext());
}
LLVMTypeRef LLVMFP128Type(void) {
  return LLVMFP128TypeInContext(LLVMGetGlobalContext());
}
LLVMTypeRef LLVMPPCFP128Type(void) {
  return LLVMPPCFP128TypeInContext(LLVMGetGlobalContext());
}
LLVMTypeRef LLVMX86MMXType(void) {
  return LLVMX86MMXTypeInContext(LLVMGetGlobalContext());
}

/*--.. Operations on function types ........................................--*/

LLVMTypeRef LLVMFunctionType(LLVMTypeRef ReturnType,
                             LLVMTypeRef *ParamTypes, unsigned ParamCount,
                             LLVMBool IsVarArg) {
  ArrayRef<Type*> Tys(unwrap(ParamTypes), ParamCount);
  return wrap(FunctionType::get(unwrap(ReturnType), Tys, IsVarArg != 0));
}

LLVMBool LLVMIsFunctionVarArg(LLVMTypeRef FunctionTy) {
  return unwrap<FunctionType>(FunctionTy)->isVarArg();
}

LLVMTypeRef LLVMGetReturnType(LLVMTypeRef FunctionTy) {
  return wrap(unwrap<FunctionType>(FunctionTy)->getReturnType());
}

unsigned LLVMCountParamTypes(LLVMTypeRef FunctionTy) {
  return unwrap<FunctionType>(FunctionTy)->getNumParams();
}

void LLVMGetParamTypes(LLVMTypeRef FunctionTy, LLVMTypeRef *Dest) {
  FunctionType *Ty = unwrap<FunctionType>(FunctionTy);
  for (FunctionType::param_iterator I = Ty->param_begin(),
                                    E = Ty->param_end(); I != E; ++I)
    *Dest++ = wrap(*I);
}

/*--.. Operations on struct types ..........................................--*/

LLVMTypeRef LLVMStructTypeInContext(LLVMContextRef C, LLVMTypeRef *ElementTypes,
                           unsigned ElementCount, LLVMBool Packed) {
  ArrayRef<Type*> Tys(unwrap(ElementTypes), ElementCount);
  return wrap(StructType::get(*unwrap(C), Tys, Packed != 0));
}

LLVMTypeRef LLVMStructType(LLVMTypeRef *ElementTypes,
                           unsigned ElementCount, LLVMBool Packed) {
  return LLVMStructTypeInContext(LLVMGetGlobalContext(), ElementTypes,
                                 ElementCount, Packed);
}

LLVMTypeRef LLVMStructCreateNamed(LLVMContextRef C, const char *Name)
{
  return wrap(StructType::create(*unwrap(C), Name));
}

void LLVMStructSetBody(LLVMTypeRef StructTy, LLVMTypeRef *ElementTypes,
                       unsigned ElementCount, LLVMBool Packed) {
  ArrayRef<Type*> Tys(unwrap(ElementTypes), ElementCount);
  unwrap<StructType>(StructTy)->setBody(Tys, Packed != 0);
}

unsigned LLVMCountStructElementTypes(LLVMTypeRef StructTy) {
  return unwrap<StructType>(StructTy)->getNumElements();
}

void LLVMGetStructElementTypes(LLVMTypeRef StructTy, LLVMTypeRef *Dest) {
  StructType *Ty = unwrap<StructType>(StructTy);
  for (StructType::element_iterator I = Ty->element_begin(),
                                    E = Ty->element_end(); I != E; ++I)
    *Dest++ = wrap(*I);
}

LLVMBool LLVMIsPackedStruct(LLVMTypeRef StructTy) {
  return unwrap<StructType>(StructTy)->isPacked();
}

LLVMBool LLVMIsOpaqueStruct(LLVMTypeRef StructTy) {
  return unwrap<StructType>(StructTy)->isOpaque();
}

LLVMTypeRef LLVMGetTypeByName(LLVMModuleRef M, const char *Name) {
  return wrap(unwrap(M)->getTypeByName(Name));
}

/*--.. Operations on array, pointer, and vector types (sequence types) .....--*/

LLVMTypeRef LLVMArrayType(LLVMTypeRef ElementType, unsigned ElementCount) {
  return wrap(ArrayType::get(unwrap(ElementType), ElementCount));
}

LLVMTypeRef LLVMPointerType(LLVMTypeRef ElementType, unsigned AddressSpace) {
  return wrap(PointerType::get(unwrap(ElementType), AddressSpace));
}

LLVMTypeRef LLVMVectorType(LLVMTypeRef ElementType, unsigned ElementCount) {
  return wrap(VectorType::get(unwrap(ElementType), ElementCount));
}

LLVMTypeRef LLVMGetElementType(LLVMTypeRef Ty) {
  return wrap(unwrap<SequentialType>(Ty)->getElementType());
}

unsigned LLVMGetArrayLength(LLVMTypeRef ArrayTy) {
  return unwrap<ArrayType>(ArrayTy)->getNumElements();
}

unsigned LLVMGetPointerAddressSpace(LLVMTypeRef PointerTy) {
  return unwrap<PointerType>(PointerTy)->getAddressSpace();
}

unsigned LLVMGetVectorSize(LLVMTypeRef VectorTy) {
  return unwrap<VectorType>(VectorTy)->getNumElements();
}

/*--.. Operations on other types ...........................................--*/

LLVMTypeRef LLVMVoidTypeInContext(LLVMContextRef C)  {
  return wrap(Type::getVoidTy(*unwrap(C)));
}
LLVMTypeRef LLVMLabelTypeInContext(LLVMContextRef C) {
  return wrap(Type::getLabelTy(*unwrap(C)));
}

LLVMTypeRef LLVMVoidType(void)  {
  return LLVMVoidTypeInContext(LLVMGetGlobalContext());
}
LLVMTypeRef LLVMLabelType(void) {
  return LLVMLabelTypeInContext(LLVMGetGlobalContext());
}

/*===-- Operations on values ----------------------------------------------===*/

/*--.. Operations on all values ............................................--*/

LLVMTypeRef LLVMTypeOf(LLVMValueRef Val) {
  return wrap(unwrap(Val)->getType());
}

const char *LLVMGetValueName(LLVMValueRef Val) {
  return unwrap(Val)->getName().data();
}

void LLVMSetValueName(LLVMValueRef Val, const char *Name) {
  unwrap(Val)->setName(Name);
}

void LLVMDumpValue(LLVMValueRef Val) {
  unwrap(Val)->dump();
}

void LLVMReplaceAllUsesWith(LLVMValueRef OldVal, LLVMValueRef NewVal) {
  unwrap(OldVal)->replaceAllUsesWith(unwrap(NewVal));
}

int LLVMHasMetadata(LLVMValueRef Inst) {
  return unwrap<Instruction>(Inst)->hasMetadata();
}

LLVMValueRef LLVMGetMetadata(LLVMValueRef Inst, unsigned KindID) {
  return wrap(unwrap<Instruction>(Inst)->getMetadata(KindID));
}

void LLVMSetMetadata(LLVMValueRef Inst, unsigned KindID, LLVMValueRef MD) {
  unwrap<Instruction>(Inst)->setMetadata(KindID, MD? unwrap<MDNode>(MD) : NULL);
}

/*--.. Conversion functions ................................................--*/

#define LLVM_DEFINE_VALUE_CAST(name)                                       \
  LLVMValueRef LLVMIsA##name(LLVMValueRef Val) {                           \
    return wrap(static_cast<Value*>(dyn_cast_or_null<name>(unwrap(Val)))); \
  }

LLVM_FOR_EACH_VALUE_SUBCLASS(LLVM_DEFINE_VALUE_CAST)

/*--.. Operations on Uses ..................................................--*/
LLVMUseRef LLVMGetFirstUse(LLVMValueRef Val) {
  Value *V = unwrap(Val);
  Value::use_iterator I = V->use_begin();
  if (I == V->use_end())
    return 0;
  return wrap(&(I.getUse()));
}

LLVMUseRef LLVMGetNextUse(LLVMUseRef U) {
  Use *Next = unwrap(U)->getNext();
  if (Next)
    return wrap(Next);
  return 0;
}

LLVMValueRef LLVMGetUser(LLVMUseRef U) {
  return wrap(unwrap(U)->getUser());
}

LLVMValueRef LLVMGetUsedValue(LLVMUseRef U) {
  return wrap(unwrap(U)->get());
}

/*--.. Operations on Users .................................................--*/
LLVMValueRef LLVMGetOperand(LLVMValueRef Val, unsigned Index) {
  return wrap(unwrap<User>(Val)->getOperand(Index));
}

void LLVMSetOperand(LLVMValueRef Val, unsigned Index, LLVMValueRef Op) {
  unwrap<User>(Val)->setOperand(Index, unwrap(Op));
}

int LLVMGetNumOperands(LLVMValueRef Val) {
  return unwrap<User>(Val)->getNumOperands();
}

/*--.. Operations on constants of any type .................................--*/

LLVMValueRef LLVMConstNull(LLVMTypeRef Ty) {
  return wrap(Constant::getNullValue(unwrap(Ty)));
}

LLVMValueRef LLVMConstAllOnes(LLVMTypeRef Ty) {
  return wrap(Constant::getAllOnesValue(unwrap(Ty)));
}

LLVMValueRef LLVMGetUndef(LLVMTypeRef Ty) {
  return wrap(UndefValue::get(unwrap(Ty)));
}

LLVMBool LLVMIsConstant(LLVMValueRef Ty) {
  return isa<Constant>(unwrap(Ty));
}

LLVMBool LLVMIsNull(LLVMValueRef Val) {
  if (Constant *C = dyn_cast<Constant>(unwrap(Val)))
    return C->isNullValue();
  return false;
}

LLVMBool LLVMIsUndef(LLVMValueRef Val) {
  return isa<UndefValue>(unwrap(Val));
}

LLVMValueRef LLVMConstPointerNull(LLVMTypeRef Ty) {
  return
      wrap(ConstantPointerNull::get(unwrap<PointerType>(Ty)));
}

/*--.. Operations on metadata nodes ........................................--*/

LLVMValueRef LLVMMDStringInContext(LLVMContextRef C, const char *Str,
                                   unsigned SLen) {
  return wrap(MDString::get(*unwrap(C), StringRef(Str, SLen)));
}

LLVMValueRef LLVMMDString(const char *Str, unsigned SLen) {
  return LLVMMDStringInContext(LLVMGetGlobalContext(), Str, SLen);
}

LLVMValueRef LLVMMDNodeInContext(LLVMContextRef C, LLVMValueRef *Vals,
                                 unsigned Count) {
  return wrap(MDNode::get(*unwrap(C),
                          makeArrayRef(unwrap<Value>(Vals, Count), Count)));
}

LLVMValueRef LLVMMDNode(LLVMValueRef *Vals, unsigned Count) {
  return LLVMMDNodeInContext(LLVMGetGlobalContext(), Vals, Count);
}

/*--.. Operations on scalar constants ......................................--*/

LLVMValueRef LLVMConstInt(LLVMTypeRef IntTy, unsigned long long N,
                          LLVMBool SignExtend) {
  return wrap(ConstantInt::get(unwrap<IntegerType>(IntTy), N, SignExtend != 0));
}

LLVMValueRef LLVMConstIntOfArbitraryPrecision(LLVMTypeRef IntTy,
                                              unsigned NumWords,
                                              const uint64_t Words[]) {
    IntegerType *Ty = unwrap<IntegerType>(IntTy);
    return wrap(ConstantInt::get(Ty->getContext(),
                                 APInt(Ty->getBitWidth(),
                                       makeArrayRef(Words, NumWords))));
}

LLVMValueRef LLVMConstIntOfString(LLVMTypeRef IntTy, const char Str[],
                                  uint8_t Radix) {
  return wrap(ConstantInt::get(unwrap<IntegerType>(IntTy), StringRef(Str),
                               Radix));
}

LLVMValueRef LLVMConstIntOfStringAndSize(LLVMTypeRef IntTy, const char Str[],
                                         unsigned SLen, uint8_t Radix) {
  return wrap(ConstantInt::get(unwrap<IntegerType>(IntTy), StringRef(Str, SLen),
                               Radix));
}

LLVMValueRef LLVMConstReal(LLVMTypeRef RealTy, double N) {
  return wrap(ConstantFP::get(unwrap(RealTy), N));
}

LLVMValueRef LLVMConstRealOfString(LLVMTypeRef RealTy, const char *Text) {
  return wrap(ConstantFP::get(unwrap(RealTy), StringRef(Text)));
}

LLVMValueRef LLVMConstRealOfStringAndSize(LLVMTypeRef RealTy, const char Str[],
                                          unsigned SLen) {
  return wrap(ConstantFP::get(unwrap(RealTy), StringRef(Str, SLen)));
}

unsigned long long LLVMConstIntGetZExtValue(LLVMValueRef ConstantVal) {
  return unwrap<ConstantInt>(ConstantVal)->getZExtValue();
}

long long LLVMConstIntGetSExtValue(LLVMValueRef ConstantVal) {
  return unwrap<ConstantInt>(ConstantVal)->getSExtValue();
}

/*--.. Operations on composite constants ...................................--*/

LLVMValueRef LLVMConstStringInContext(LLVMContextRef C, const char *Str,
                                      unsigned Length,
                                      LLVMBool DontNullTerminate) {
  /* Inverted the sense of AddNull because ', 0)' is a
     better mnemonic for null termination than ', 1)'. */
  return wrap(ConstantArray::get(*unwrap(C), StringRef(Str, Length),
                                 DontNullTerminate == 0));
}
LLVMValueRef LLVMConstStructInContext(LLVMContextRef C, 
                                      LLVMValueRef *ConstantVals,
                                      unsigned Count, LLVMBool Packed) {
  Constant **Elements = unwrap<Constant>(ConstantVals, Count);
  return wrap(ConstantStruct::getAnon(*unwrap(C), makeArrayRef(Elements, Count),
                                      Packed != 0));
}

LLVMValueRef LLVMConstString(const char *Str, unsigned Length,
                             LLVMBool DontNullTerminate) {
  return LLVMConstStringInContext(LLVMGetGlobalContext(), Str, Length,
                                  DontNullTerminate);
}
LLVMValueRef LLVMConstArray(LLVMTypeRef ElementTy,
                            LLVMValueRef *ConstantVals, unsigned Length) {
  ArrayRef<Constant*> V(unwrap<Constant>(ConstantVals, Length), Length);
  return wrap(ConstantArray::get(ArrayType::get(unwrap(ElementTy), Length), V));
}
LLVMValueRef LLVMConstStruct(LLVMValueRef *ConstantVals, unsigned Count,
                             LLVMBool Packed) {
  return LLVMConstStructInContext(LLVMGetGlobalContext(), ConstantVals, Count,
                                  Packed);
}

LLVMValueRef LLVMConstNamedStruct(LLVMTypeRef StructTy,
                                  LLVMValueRef *ConstantVals,
                                  unsigned Count) {
  Constant **Elements = unwrap<Constant>(ConstantVals, Count);
  StructType *Ty = cast<StructType>(unwrap(StructTy));

  return wrap(ConstantStruct::get(Ty, makeArrayRef(Elements, Count)));
}

LLVMValueRef LLVMConstVector(LLVMValueRef *ScalarConstantVals, unsigned Size) {
  return wrap(ConstantVector::get(makeArrayRef(
                            unwrap<Constant>(ScalarConstantVals, Size), Size)));
}
/*--.. Constant expressions ................................................--*/

LLVMOpcode LLVMGetConstOpcode(LLVMValueRef ConstantVal) {
  return (LLVMOpcode)unwrap<ConstantExpr>(ConstantVal)->getOpcode();
}

LLVMValueRef LLVMAlignOf(LLVMTypeRef Ty) {
  return wrap(ConstantExpr::getAlignOf(unwrap(Ty)));
}

LLVMValueRef LLVMSizeOf(LLVMTypeRef Ty) {
  return wrap(ConstantExpr::getSizeOf(unwrap(Ty)));
}

LLVMValueRef LLVMConstNeg(LLVMValueRef ConstantVal) {
  return wrap(ConstantExpr::getNeg(unwrap<Constant>(ConstantVal)));
}

LLVMValueRef LLVMConstNSWNeg(LLVMValueRef ConstantVal) {
  return wrap(ConstantExpr::getNSWNeg(unwrap<Constant>(ConstantVal)));
}

LLVMValueRef LLVMConstNUWNeg(LLVMValueRef ConstantVal) {
  return wrap(ConstantExpr::getNUWNeg(unwrap<Constant>(ConstantVal)));
}


LLVMValueRef LLVMConstFNeg(LLVMValueRef ConstantVal) {
  return wrap(ConstantExpr::getFNeg(unwrap<Constant>(ConstantVal)));
}

LLVMValueRef LLVMConstNot(LLVMValueRef ConstantVal) {
  return wrap(ConstantExpr::getNot(unwrap<Constant>(ConstantVal)));
}

LLVMValueRef LLVMConstAdd(LLVMValueRef LHSConstant, LLVMValueRef RHSConstant) {
  return wrap(ConstantExpr::getAdd(unwrap<Constant>(LHSConstant),
                                   unwrap<Constant>(RHSConstant)));
}

LLVMValueRef LLVMConstNSWAdd(LLVMValueRef LHSConstant,
                             LLVMValueRef RHSConstant) {
  return wrap(ConstantExpr::getNSWAdd(unwrap<Constant>(LHSConstant),
                                      unwrap<Constant>(RHSConstant)));
}

LLVMValueRef LLVMConstNUWAdd(LLVMValueRef LHSConstant,
                             LLVMValueRef RHSConstant) {
  return wrap(ConstantExpr::getNUWAdd(unwrap<Constant>(LHSConstant),
                                      unwrap<Constant>(RHSConstant)));
}

LLVMValueRef LLVMConstFAdd(LLVMValueRef LHSConstant, LLVMValueRef RHSConstant) {
  return wrap(ConstantExpr::getFAdd(unwrap<Constant>(LHSConstant),
                                    unwrap<Constant>(RHSConstant)));
}

LLVMValueRef LLVMConstSub(LLVMValueRef LHSConstant, LLVMValueRef RHSConstant) {
  return wrap(ConstantExpr::getSub(unwrap<Constant>(LHSConstant),
                                   unwrap<Constant>(RHSConstant)));
}

LLVMValueRef LLVMConstNSWSub(LLVMValueRef LHSConstant,
                             LLVMValueRef RHSConstant) {
  return wrap(ConstantExpr::getNSWSub(unwrap<Constant>(LHSConstant),
                                      unwrap<Constant>(RHSConstant)));
}

LLVMValueRef LLVMConstNUWSub(LLVMValueRef LHSConstant,
                             LLVMValueRef RHSConstant) {
  return wrap(ConstantExpr::getNUWSub(unwrap<Constant>(LHSConstant),
                                      unwrap<Constant>(RHSConstant)));
}

LLVMValueRef LLVMConstFSub(LLVMValueRef LHSConstant, LLVMValueRef RHSConstant) {
  return wrap(ConstantExpr::getFSub(unwrap<Constant>(LHSConstant),
                                    unwrap<Constant>(RHSConstant)));
}

LLVMValueRef LLVMConstMul(LLVMValueRef LHSConstant, LLVMValueRef RHSConstant) {
  return wrap(ConstantExpr::getMul(unwrap<Constant>(LHSConstant),
                                   unwrap<Constant>(RHSConstant)));
}

LLVMValueRef LLVMConstNSWMul(LLVMValueRef LHSConstant,
                             LLVMValueRef RHSConstant) {
  return wrap(ConstantExpr::getNSWMul(unwrap<Constant>(LHSConstant),
                                      unwrap<Constant>(RHSConstant)));
}

LLVMValueRef LLVMConstNUWMul(LLVMValueRef LHSConstant,
                             LLVMValueRef RHSConstant) {
  return wrap(ConstantExpr::getNUWMul(unwrap<Constant>(LHSConstant),
                                      unwrap<Constant>(RHSConstant)));
}

LLVMValueRef LLVMConstFMul(LLVMValueRef LHSConstant, LLVMValueRef RHSConstant) {
  return wrap(ConstantExpr::getFMul(unwrap<Constant>(LHSConstant),
                                    unwrap<Constant>(RHSConstant)));
}

LLVMValueRef LLVMConstUDiv(LLVMValueRef LHSConstant, LLVMValueRef RHSConstant) {
  return wrap(ConstantExpr::getUDiv(unwrap<Constant>(LHSConstant),
                                    unwrap<Constant>(RHSConstant)));
}

LLVMValueRef LLVMConstSDiv(LLVMValueRef LHSConstant, LLVMValueRef RHSConstant) {
  return wrap(ConstantExpr::getSDiv(unwrap<Constant>(LHSConstant),
                                    unwrap<Constant>(RHSConstant)));
}

LLVMValueRef LLVMConstExactSDiv(LLVMValueRef LHSConstant,
                                LLVMValueRef RHSConstant) {
  return wrap(ConstantExpr::getExactSDiv(unwrap<Constant>(LHSConstant),
                                         unwrap<Constant>(RHSConstant)));
}

LLVMValueRef LLVMConstFDiv(LLVMValueRef LHSConstant, LLVMValueRef RHSConstant) {
  return wrap(ConstantExpr::getFDiv(unwrap<Constant>(LHSConstant),
                                    unwrap<Constant>(RHSConstant)));
}

LLVMValueRef LLVMConstURem(LLVMValueRef LHSConstant, LLVMValueRef RHSConstant) {
  return wrap(ConstantExpr::getURem(unwrap<Constant>(LHSConstant),
                                    unwrap<Constant>(RHSConstant)));
}

LLVMValueRef LLVMConstSRem(LLVMValueRef LHSConstant, LLVMValueRef RHSConstant) {
  return wrap(ConstantExpr::getSRem(unwrap<Constant>(LHSConstant),
                                    unwrap<Constant>(RHSConstant)));
}

LLVMValueRef LLVMConstFRem(LLVMValueRef LHSConstant, LLVMValueRef RHSConstant) {
  return wrap(ConstantExpr::getFRem(unwrap<Constant>(LHSConstant),
                                    unwrap<Constant>(RHSConstant)));
}

LLVMValueRef LLVMConstAnd(LLVMValueRef LHSConstant, LLVMValueRef RHSConstant) {
  return wrap(ConstantExpr::getAnd(unwrap<Constant>(LHSConstant),
                                   unwrap<Constant>(RHSConstant)));
}

LLVMValueRef LLVMConstOr(LLVMValueRef LHSConstant, LLVMValueRef RHSConstant) {
  return wrap(ConstantExpr::getOr(unwrap<Constant>(LHSConstant),
                                  unwrap<Constant>(RHSConstant)));
}

LLVMValueRef LLVMConstXor(LLVMValueRef LHSConstant, LLVMValueRef RHSConstant) {
  return wrap(ConstantExpr::getXor(unwrap<Constant>(LHSConstant),
                                   unwrap<Constant>(RHSConstant)));
}

LLVMValueRef LLVMConstICmp(LLVMIntPredicate Predicate,
                           LLVMValueRef LHSConstant, LLVMValueRef RHSConstant) {
  return wrap(ConstantExpr::getICmp(Predicate,
                                    unwrap<Constant>(LHSConstant),
                                    unwrap<Constant>(RHSConstant)));
}

LLVMValueRef LLVMConstFCmp(LLVMRealPredicate Predicate,
                           LLVMValueRef LHSConstant, LLVMValueRef RHSConstant) {
  return wrap(ConstantExpr::getFCmp(Predicate,
                                    unwrap<Constant>(LHSConstant),
                                    unwrap<Constant>(RHSConstant)));
}

LLVMValueRef LLVMConstShl(LLVMValueRef LHSConstant, LLVMValueRef RHSConstant) {
  return wrap(ConstantExpr::getShl(unwrap<Constant>(LHSConstant),
                                   unwrap<Constant>(RHSConstant)));
}

LLVMValueRef LLVMConstLShr(LLVMValueRef LHSConstant, LLVMValueRef RHSConstant) {
  return wrap(ConstantExpr::getLShr(unwrap<Constant>(LHSConstant),
                                    unwrap<Constant>(RHSConstant)));
}

LLVMValueRef LLVMConstAShr(LLVMValueRef LHSConstant, LLVMValueRef RHSConstant) {
  return wrap(ConstantExpr::getAShr(unwrap<Constant>(LHSConstant),
                                    unwrap<Constant>(RHSConstant)));
}

LLVMValueRef LLVMConstGEP(LLVMValueRef ConstantVal,
                          LLVMValueRef *ConstantIndices, unsigned NumIndices) {
  ArrayRef<Constant *> IdxList(unwrap<Constant>(ConstantIndices, NumIndices),
                               NumIndices);
  return wrap(ConstantExpr::getGetElementPtr(unwrap<Constant>(ConstantVal),
                                             IdxList));
}

LLVMValueRef LLVMConstInBoundsGEP(LLVMValueRef ConstantVal,
                                  LLVMValueRef *ConstantIndices,
                                  unsigned NumIndices) {
  Constant* Val = unwrap<Constant>(ConstantVal);
  ArrayRef<Constant *> IdxList(unwrap<Constant>(ConstantIndices, NumIndices),
                               NumIndices);
  return wrap(ConstantExpr::getInBoundsGetElementPtr(Val, IdxList));
}

LLVMValueRef LLVMConstTrunc(LLVMValueRef ConstantVal, LLVMTypeRef ToType) {
  return wrap(ConstantExpr::getTrunc(unwrap<Constant>(ConstantVal),
                                     unwrap(ToType)));
}

LLVMValueRef LLVMConstSExt(LLVMValueRef ConstantVal, LLVMTypeRef ToType) {
  return wrap(ConstantExpr::getSExt(unwrap<Constant>(ConstantVal),
                                    unwrap(ToType)));
}

LLVMValueRef LLVMConstZExt(LLVMValueRef ConstantVal, LLVMTypeRef ToType) {
  return wrap(ConstantExpr::getZExt(unwrap<Constant>(ConstantVal),
                                    unwrap(ToType)));
}

LLVMValueRef LLVMConstFPTrunc(LLVMValueRef ConstantVal, LLVMTypeRef ToType) {
  return wrap(ConstantExpr::getFPTrunc(unwrap<Constant>(ConstantVal),
                                       unwrap(ToType)));
}

LLVMValueRef LLVMConstFPExt(LLVMValueRef ConstantVal, LLVMTypeRef ToType) {
  return wrap(ConstantExpr::getFPExtend(unwrap<Constant>(ConstantVal),
                                        unwrap(ToType)));
}

LLVMValueRef LLVMConstUIToFP(LLVMValueRef ConstantVal, LLVMTypeRef ToType) {
  return wrap(ConstantExpr::getUIToFP(unwrap<Constant>(ConstantVal),
                                      unwrap(ToType)));
}

LLVMValueRef LLVMConstSIToFP(LLVMValueRef ConstantVal, LLVMTypeRef ToType) {
  return wrap(ConstantExpr::getSIToFP(unwrap<Constant>(ConstantVal),
                                      unwrap(ToType)));
}

LLVMValueRef LLVMConstFPToUI(LLVMValueRef ConstantVal, LLVMTypeRef ToType) {
  return wrap(ConstantExpr::getFPToUI(unwrap<Constant>(ConstantVal),
                                      unwrap(ToType)));
}

LLVMValueRef LLVMConstFPToSI(LLVMValueRef ConstantVal, LLVMTypeRef ToType) {
  return wrap(ConstantExpr::getFPToSI(unwrap<Constant>(ConstantVal),
                                      unwrap(ToType)));
}

LLVMValueRef LLVMConstPtrToInt(LLVMValueRef ConstantVal, LLVMTypeRef ToType) {
  return wrap(ConstantExpr::getPtrToInt(unwrap<Constant>(ConstantVal),
                                        unwrap(ToType)));
}

LLVMValueRef LLVMConstIntToPtr(LLVMValueRef ConstantVal, LLVMTypeRef ToType) {
  return wrap(ConstantExpr::getIntToPtr(unwrap<Constant>(ConstantVal),
                                        unwrap(ToType)));
}

LLVMValueRef LLVMConstBitCast(LLVMValueRef ConstantVal, LLVMTypeRef ToType) {
  return wrap(ConstantExpr::getBitCast(unwrap<Constant>(ConstantVal),
                                       unwrap(ToType)));
}

LLVMValueRef LLVMConstZExtOrBitCast(LLVMValueRef ConstantVal,
                                    LLVMTypeRef ToType) {
  return wrap(ConstantExpr::getZExtOrBitCast(unwrap<Constant>(ConstantVal),
                                             unwrap(ToType)));
}

LLVMValueRef LLVMConstSExtOrBitCast(LLVMValueRef ConstantVal,
                                    LLVMTypeRef ToType) {
  return wrap(ConstantExpr::getSExtOrBitCast(unwrap<Constant>(ConstantVal),
                                             unwrap(ToType)));
}

LLVMValueRef LLVMConstTruncOrBitCast(LLVMValueRef ConstantVal,
                                     LLVMTypeRef ToType) {
  return wrap(ConstantExpr::getTruncOrBitCast(unwrap<Constant>(ConstantVal),
                                              unwrap(ToType)));
}

LLVMValueRef LLVMConstPointerCast(LLVMValueRef ConstantVal,
                                  LLVMTypeRef ToType) {
  return wrap(ConstantExpr::getPointerCast(unwrap<Constant>(ConstantVal),
                                           unwrap(ToType)));
}

LLVMValueRef LLVMConstIntCast(LLVMValueRef ConstantVal, LLVMTypeRef ToType,
                              LLVMBool isSigned) {
  return wrap(ConstantExpr::getIntegerCast(unwrap<Constant>(ConstantVal),
                                           unwrap(ToType), isSigned));
}

LLVMValueRef LLVMConstFPCast(LLVMValueRef ConstantVal, LLVMTypeRef ToType) {
  return wrap(ConstantExpr::getFPCast(unwrap<Constant>(ConstantVal),
                                      unwrap(ToType)));
}

LLVMValueRef LLVMConstSelect(LLVMValueRef ConstantCondition,
                             LLVMValueRef ConstantIfTrue,
                             LLVMValueRef ConstantIfFalse) {
  return wrap(ConstantExpr::getSelect(unwrap<Constant>(ConstantCondition),
                                      unwrap<Constant>(ConstantIfTrue),
                                      unwrap<Constant>(ConstantIfFalse)));
}

LLVMValueRef LLVMConstExtractElement(LLVMValueRef VectorConstant,
                                     LLVMValueRef IndexConstant) {
  return wrap(ConstantExpr::getExtractElement(unwrap<Constant>(VectorConstant),
                                              unwrap<Constant>(IndexConstant)));
}

LLVMValueRef LLVMConstInsertElement(LLVMValueRef VectorConstant,
                                    LLVMValueRef ElementValueConstant,
                                    LLVMValueRef IndexConstant) {
  return wrap(ConstantExpr::getInsertElement(unwrap<Constant>(VectorConstant),
                                         unwrap<Constant>(ElementValueConstant),
                                             unwrap<Constant>(IndexConstant)));
}

LLVMValueRef LLVMConstShuffleVector(LLVMValueRef VectorAConstant,
                                    LLVMValueRef VectorBConstant,
                                    LLVMValueRef MaskConstant) {
  return wrap(ConstantExpr::getShuffleVector(unwrap<Constant>(VectorAConstant),
                                             unwrap<Constant>(VectorBConstant),
                                             unwrap<Constant>(MaskConstant)));
}

LLVMValueRef LLVMConstExtractValue(LLVMValueRef AggConstant, unsigned *IdxList,
                                   unsigned NumIdx) {
  return wrap(ConstantExpr::getExtractValue(unwrap<Constant>(AggConstant),
                                            makeArrayRef(IdxList, NumIdx)));
}

LLVMValueRef LLVMConstInsertValue(LLVMValueRef AggConstant,
                                  LLVMValueRef ElementValueConstant,
                                  unsigned *IdxList, unsigned NumIdx) {
  return wrap(ConstantExpr::getInsertValue(unwrap<Constant>(AggConstant),
                                         unwrap<Constant>(ElementValueConstant),
                                           makeArrayRef(IdxList, NumIdx)));
}

LLVMValueRef LLVMConstInlineAsm(LLVMTypeRef Ty, const char *AsmString,
                                const char *Constraints,
                                LLVMBool HasSideEffects,
                                LLVMBool IsAlignStack) {
  return wrap(InlineAsm::get(dyn_cast<FunctionType>(unwrap(Ty)), AsmString,
                             Constraints, HasSideEffects, IsAlignStack));
}

LLVMValueRef LLVMBlockAddress(LLVMValueRef F, LLVMBasicBlockRef BB) {
  return wrap(BlockAddress::get(unwrap<Function>(F), unwrap(BB)));
}

/*--.. Operations on global variables, functions, and aliases (globals) ....--*/

LLVMModuleRef LLVMGetGlobalParent(LLVMValueRef Global) {
  return wrap(unwrap<GlobalValue>(Global)->getParent());
}

LLVMBool LLVMIsDeclaration(LLVMValueRef Global) {
  return unwrap<GlobalValue>(Global)->isDeclaration();
}

LLVMLinkage LLVMGetLinkage(LLVMValueRef Global) {
  switch (unwrap<GlobalValue>(Global)->getLinkage()) {
  default:
    assert(false && "Unhandled Linkage Type.");
  case GlobalValue::ExternalLinkage:
    return LLVMExternalLinkage;
  case GlobalValue::AvailableExternallyLinkage:
    return LLVMAvailableExternallyLinkage;
  case GlobalValue::LinkOnceAnyLinkage:
    return LLVMLinkOnceAnyLinkage;
  case GlobalValue::LinkOnceODRLinkage:
    return LLVMLinkOnceODRLinkage;
  case GlobalValue::WeakAnyLinkage:
    return LLVMWeakAnyLinkage;
  case GlobalValue::WeakODRLinkage:
    return LLVMWeakODRLinkage;
  case GlobalValue::AppendingLinkage:
    return LLVMAppendingLinkage;
  case GlobalValue::InternalLinkage:
    return LLVMInternalLinkage;
  case GlobalValue::PrivateLinkage:
    return LLVMPrivateLinkage;
  case GlobalValue::LinkerPrivateLinkage:
    return LLVMLinkerPrivateLinkage;
  case GlobalValue::LinkerPrivateWeakLinkage:
    return LLVMLinkerPrivateWeakLinkage;
  case GlobalValue::LinkerPrivateWeakDefAutoLinkage:
    return LLVMLinkerPrivateWeakDefAutoLinkage;
  case GlobalValue::DLLImportLinkage:
    return LLVMDLLImportLinkage;
  case GlobalValue::DLLExportLinkage:
    return LLVMDLLExportLinkage;
  case GlobalValue::ExternalWeakLinkage:
    return LLVMExternalWeakLinkage;
  case GlobalValue::CommonLinkage:
    return LLVMCommonLinkage;
  }

  // Should never get here.
  return static_cast<LLVMLinkage>(0);
}

void LLVMSetLinkage(LLVMValueRef Global, LLVMLinkage Linkage) {
  GlobalValue *GV = unwrap<GlobalValue>(Global);

  switch (Linkage) {
  default:
    assert(false && "Unhandled Linkage Type.");
  case LLVMExternalLinkage:
    GV->setLinkage(GlobalValue::ExternalLinkage);
    break;
  case LLVMAvailableExternallyLinkage:
    GV->setLinkage(GlobalValue::AvailableExternallyLinkage);
    break;
  case LLVMLinkOnceAnyLinkage:
    GV->setLinkage(GlobalValue::LinkOnceAnyLinkage);
    break;
  case LLVMLinkOnceODRLinkage:
    GV->setLinkage(GlobalValue::LinkOnceODRLinkage);
    break;
  case LLVMWeakAnyLinkage:
    GV->setLinkage(GlobalValue::WeakAnyLinkage);
    break;
  case LLVMWeakODRLinkage:
    GV->setLinkage(GlobalValue::WeakODRLinkage);
    break;
  case LLVMAppendingLinkage:
    GV->setLinkage(GlobalValue::AppendingLinkage);
    break;
  case LLVMInternalLinkage:
    GV->setLinkage(GlobalValue::InternalLinkage);
    break;
  case LLVMPrivateLinkage:
    GV->setLinkage(GlobalValue::PrivateLinkage);
    break;
  case LLVMLinkerPrivateLinkage:
    GV->setLinkage(GlobalValue::LinkerPrivateLinkage);
    break;
  case LLVMLinkerPrivateWeakLinkage:
    GV->setLinkage(GlobalValue::LinkerPrivateWeakLinkage);
    break;
  case LLVMLinkerPrivateWeakDefAutoLinkage:
    GV->setLinkage(GlobalValue::LinkerPrivateWeakDefAutoLinkage);
    break;
  case LLVMDLLImportLinkage:
    GV->setLinkage(GlobalValue::DLLImportLinkage);
    break;
  case LLVMDLLExportLinkage:
    GV->setLinkage(GlobalValue::DLLExportLinkage);
    break;
  case LLVMExternalWeakLinkage:
    GV->setLinkage(GlobalValue::ExternalWeakLinkage);
    break;
  case LLVMGhostLinkage:
    DEBUG(errs()
          << "LLVMSetLinkage(): LLVMGhostLinkage is no longer supported.");
    break;
  case LLVMCommonLinkage:
    GV->setLinkage(GlobalValue::CommonLinkage);
    break;
  }
}

const char *LLVMGetSection(LLVMValueRef Global) {
  return unwrap<GlobalValue>(Global)->getSection().c_str();
}

void LLVMSetSection(LLVMValueRef Global, const char *Section) {
  unwrap<GlobalValue>(Global)->setSection(Section);
}

LLVMVisibility LLVMGetVisibility(LLVMValueRef Global) {
  return static_cast<LLVMVisibility>(
    unwrap<GlobalValue>(Global)->getVisibility());
}

void LLVMSetVisibility(LLVMValueRef Global, LLVMVisibility Viz) {
  unwrap<GlobalValue>(Global)
    ->setVisibility(static_cast<GlobalValue::VisibilityTypes>(Viz));
}

unsigned LLVMGetAlignment(LLVMValueRef Global) {
  return unwrap<GlobalValue>(Global)->getAlignment();
}

void LLVMSetAlignment(LLVMValueRef Global, unsigned Bytes) {
  unwrap<GlobalValue>(Global)->setAlignment(Bytes);
}

/*--.. Operations on global variables ......................................--*/

LLVMValueRef LLVMAddGlobal(LLVMModuleRef M, LLVMTypeRef Ty, const char *Name) {
  return wrap(new GlobalVariable(*unwrap(M), unwrap(Ty), false,
                                 GlobalValue::ExternalLinkage, 0, Name));
}

LLVMValueRef LLVMAddGlobalInAddressSpace(LLVMModuleRef M, LLVMTypeRef Ty,
                                         const char *Name,
                                         unsigned AddressSpace) {
  return wrap(new GlobalVariable(*unwrap(M), unwrap(Ty), false,
                                 GlobalValue::ExternalLinkage, 0, Name, 0,
                                 false, AddressSpace));
}

LLVMValueRef LLVMGetNamedGlobal(LLVMModuleRef M, const char *Name) {
  return wrap(unwrap(M)->getNamedGlobal(Name));
}

LLVMValueRef LLVMGetFirstGlobal(LLVMModuleRef M) {
  Module *Mod = unwrap(M);
  Module::global_iterator I = Mod->global_begin();
  if (I == Mod->global_end())
    return 0;
  return wrap(I);
}

LLVMValueRef LLVMGetLastGlobal(LLVMModuleRef M) {
  Module *Mod = unwrap(M);
  Module::global_iterator I = Mod->global_end();
  if (I == Mod->global_begin())
    return 0;
  return wrap(--I);
}

LLVMValueRef LLVMGetNextGlobal(LLVMValueRef GlobalVar) {
  GlobalVariable *GV = unwrap<GlobalVariable>(GlobalVar);
  Module::global_iterator I = GV;
  if (++I == GV->getParent()->global_end())
    return 0;
  return wrap(I);
}

LLVMValueRef LLVMGetPreviousGlobal(LLVMValueRef GlobalVar) {
  GlobalVariable *GV = unwrap<GlobalVariable>(GlobalVar);
  Module::global_iterator I = GV;
  if (I == GV->getParent()->global_begin())
    return 0;
  return wrap(--I);
}

void LLVMDeleteGlobal(LLVMValueRef GlobalVar) {
  unwrap<GlobalVariable>(GlobalVar)->eraseFromParent();
}

LLVMValueRef LLVMGetInitializer(LLVMValueRef GlobalVar) {
  GlobalVariable* GV = unwrap<GlobalVariable>(GlobalVar);
  if ( !GV->hasInitializer() )
    return 0;
  return wrap(GV->getInitializer());
}

void LLVMSetInitializer(LLVMValueRef GlobalVar, LLVMValueRef ConstantVal) {
  unwrap<GlobalVariable>(GlobalVar)
    ->setInitializer(unwrap<Constant>(ConstantVal));
}

LLVMBool LLVMIsThreadLocal(LLVMValueRef GlobalVar) {
  return unwrap<GlobalVariable>(GlobalVar)->isThreadLocal();
}

void LLVMSetThreadLocal(LLVMValueRef GlobalVar, LLVMBool IsThreadLocal) {
  unwrap<GlobalVariable>(GlobalVar)->setThreadLocal(IsThreadLocal != 0);
}

LLVMBool LLVMIsGlobalConstant(LLVMValueRef GlobalVar) {
  return unwrap<GlobalVariable>(GlobalVar)->isConstant();
}

void LLVMSetGlobalConstant(LLVMValueRef GlobalVar, LLVMBool IsConstant) {
  unwrap<GlobalVariable>(GlobalVar)->setConstant(IsConstant != 0);
}

/*--.. Operations on aliases ......................................--*/

LLVMValueRef LLVMAddAlias(LLVMModuleRef M, LLVMTypeRef Ty, LLVMValueRef Aliasee,
                          const char *Name) {
  return wrap(new GlobalAlias(unwrap(Ty), GlobalValue::ExternalLinkage, Name,
                              unwrap<Constant>(Aliasee), unwrap (M)));
}

/*--.. Operations on functions .............................................--*/

LLVMValueRef LLVMAddFunction(LLVMModuleRef M, const char *Name,
                             LLVMTypeRef FunctionTy) {
  return wrap(Function::Create(unwrap<FunctionType>(FunctionTy),
                               GlobalValue::ExternalLinkage, Name, unwrap(M)));
}

LLVMValueRef LLVMGetNamedFunction(LLVMModuleRef M, const char *Name) {
  return wrap(unwrap(M)->getFunction(Name));
}

LLVMValueRef LLVMGetFirstFunction(LLVMModuleRef M) {
  Module *Mod = unwrap(M);
  Module::iterator I = Mod->begin();
  if (I == Mod->end())
    return 0;
  return wrap(I);
}

LLVMValueRef LLVMGetLastFunction(LLVMModuleRef M) {
  Module *Mod = unwrap(M);
  Module::iterator I = Mod->end();
  if (I == Mod->begin())
    return 0;
  return wrap(--I);
}

LLVMValueRef LLVMGetNextFunction(LLVMValueRef Fn) {
  Function *Func = unwrap<Function>(Fn);
  Module::iterator I = Func;
  if (++I == Func->getParent()->end())
    return 0;
  return wrap(I);
}

LLVMValueRef LLVMGetPreviousFunction(LLVMValueRef Fn) {
  Function *Func = unwrap<Function>(Fn);
  Module::iterator I = Func;
  if (I == Func->getParent()->begin())
    return 0;
  return wrap(--I);
}

void LLVMDeleteFunction(LLVMValueRef Fn) {
  unwrap<Function>(Fn)->eraseFromParent();
}

unsigned LLVMGetIntrinsicID(LLVMValueRef Fn) {
  if (Function *F = dyn_cast<Function>(unwrap(Fn)))
    return F->getIntrinsicID();
  return 0;
}

unsigned LLVMGetFunctionCallConv(LLVMValueRef Fn) {
  return unwrap<Function>(Fn)->getCallingConv();
}

void LLVMSetFunctionCallConv(LLVMValueRef Fn, unsigned CC) {
  return unwrap<Function>(Fn)->setCallingConv(
    static_cast<CallingConv::ID>(CC));
}

const char *LLVMGetGC(LLVMValueRef Fn) {
  Function *F = unwrap<Function>(Fn);
  return F->hasGC()? F->getGC() : 0;
}

void LLVMSetGC(LLVMValueRef Fn, const char *GC) {
  Function *F = unwrap<Function>(Fn);
  if (GC)
    F->setGC(GC);
  else
    F->clearGC();
}

void LLVMAddFunctionAttr(LLVMValueRef Fn, LLVMAttribute PA) {
  Function *Func = unwrap<Function>(Fn);
  const AttrListPtr PAL = Func->getAttributes();
  const AttrListPtr PALnew = PAL.addAttr(~0U, PA);
  Func->setAttributes(PALnew);
}

void LLVMRemoveFunctionAttr(LLVMValueRef Fn, LLVMAttribute PA) {
  Function *Func = unwrap<Function>(Fn);
  const AttrListPtr PAL = Func->getAttributes();
  const AttrListPtr PALnew = PAL.removeAttr(~0U, PA);
  Func->setAttributes(PALnew);
}

LLVMAttribute LLVMGetFunctionAttr(LLVMValueRef Fn) {
  Function *Func = unwrap<Function>(Fn);
  const AttrListPtr PAL = Func->getAttributes();
  Attributes attr = PAL.getFnAttributes();
  return (LLVMAttribute)attr;
}

/*--.. Operations on parameters ............................................--*/

unsigned LLVMCountParams(LLVMValueRef FnRef) {
  // This function is strictly redundant to
  //   LLVMCountParamTypes(LLVMGetElementType(LLVMTypeOf(FnRef)))
  return unwrap<Function>(FnRef)->arg_size();
}

void LLVMGetParams(LLVMValueRef FnRef, LLVMValueRef *ParamRefs) {
  Function *Fn = unwrap<Function>(FnRef);
  for (Function::arg_iterator I = Fn->arg_begin(),
                              E = Fn->arg_end(); I != E; I++)
    *ParamRefs++ = wrap(I);
}

LLVMValueRef LLVMGetParam(LLVMValueRef FnRef, unsigned index) {
  Function::arg_iterator AI = unwrap<Function>(FnRef)->arg_begin();
  while (index --> 0)
    AI++;
  return wrap(AI);
}

LLVMValueRef LLVMGetParamParent(LLVMValueRef V) {
  return wrap(unwrap<Argument>(V)->getParent());
}

LLVMValueRef LLVMGetFirstParam(LLVMValueRef Fn) {
  Function *Func = unwrap<Function>(Fn);
  Function::arg_iterator I = Func->arg_begin();
  if (I == Func->arg_end())
    return 0;
  return wrap(I);
}

LLVMValueRef LLVMGetLastParam(LLVMValueRef Fn) {
  Function *Func = unwrap<Function>(Fn);
  Function::arg_iterator I = Func->arg_end();
  if (I == Func->arg_begin())
    return 0;
  return wrap(--I);
}

LLVMValueRef LLVMGetNextParam(LLVMValueRef Arg) {
  Argument *A = unwrap<Argument>(Arg);
  Function::arg_iterator I = A;
  if (++I == A->getParent()->arg_end())
    return 0;
  return wrap(I);
}

LLVMValueRef LLVMGetPreviousParam(LLVMValueRef Arg) {
  Argument *A = unwrap<Argument>(Arg);
  Function::arg_iterator I = A;
  if (I == A->getParent()->arg_begin())
    return 0;
  return wrap(--I);
}

void LLVMAddAttribute(LLVMValueRef Arg, LLVMAttribute PA) {
  unwrap<Argument>(Arg)->addAttr(PA);
}

void LLVMRemoveAttribute(LLVMValueRef Arg, LLVMAttribute PA) {
  unwrap<Argument>(Arg)->removeAttr(PA);
}

LLVMAttribute LLVMGetAttribute(LLVMValueRef Arg) {
  Argument *A = unwrap<Argument>(Arg);
  Attributes attr = A->getParent()->getAttributes().getParamAttributes(
    A->getArgNo()+1);
  return (LLVMAttribute)attr;
}
  

void LLVMSetParamAlignment(LLVMValueRef Arg, unsigned align) {
  unwrap<Argument>(Arg)->addAttr(
          Attribute::constructAlignmentFromInt(align));
}

/*--.. Operations on basic blocks ..........................................--*/

LLVMValueRef LLVMBasicBlockAsValue(LLVMBasicBlockRef BB) {
  return wrap(static_cast<Value*>(unwrap(BB)));
}

LLVMBool LLVMValueIsBasicBlock(LLVMValueRef Val) {
  return isa<BasicBlock>(unwrap(Val));
}

LLVMBasicBlockRef LLVMValueAsBasicBlock(LLVMValueRef Val) {
  return wrap(unwrap<BasicBlock>(Val));
}

LLVMValueRef LLVMGetBasicBlockParent(LLVMBasicBlockRef BB) {
  return wrap(unwrap(BB)->getParent());
}

unsigned LLVMCountBasicBlocks(LLVMValueRef FnRef) {
  return unwrap<Function>(FnRef)->size();
}

void LLVMGetBasicBlocks(LLVMValueRef FnRef, LLVMBasicBlockRef *BasicBlocksRefs){
  Function *Fn = unwrap<Function>(FnRef);
  for (Function::iterator I = Fn->begin(), E = Fn->end(); I != E; I++)
    *BasicBlocksRefs++ = wrap(I);
}

LLVMBasicBlockRef LLVMGetEntryBasicBlock(LLVMValueRef Fn) {
  return wrap(&unwrap<Function>(Fn)->getEntryBlock());
}

LLVMBasicBlockRef LLVMGetFirstBasicBlock(LLVMValueRef Fn) {
  Function *Func = unwrap<Function>(Fn);
  Function::iterator I = Func->begin();
  if (I == Func->end())
    return 0;
  return wrap(I);
}

LLVMBasicBlockRef LLVMGetLastBasicBlock(LLVMValueRef Fn) {
  Function *Func = unwrap<Function>(Fn);
  Function::iterator I = Func->end();
  if (I == Func->begin())
    return 0;
  return wrap(--I);
}

LLVMBasicBlockRef LLVMGetNextBasicBlock(LLVMBasicBlockRef BB) {
  BasicBlock *Block = unwrap(BB);
  Function::iterator I = Block;
  if (++I == Block->getParent()->end())
    return 0;
  return wrap(I);
}

LLVMBasicBlockRef LLVMGetPreviousBasicBlock(LLVMBasicBlockRef BB) {
  BasicBlock *Block = unwrap(BB);
  Function::iterator I = Block;
  if (I == Block->getParent()->begin())
    return 0;
  return wrap(--I);
}

LLVMBasicBlockRef LLVMAppendBasicBlockInContext(LLVMContextRef C,
                                                LLVMValueRef FnRef,
                                                const char *Name) {
  return wrap(BasicBlock::Create(*unwrap(C), Name, unwrap<Function>(FnRef)));
}

LLVMBasicBlockRef LLVMAppendBasicBlock(LLVMValueRef FnRef, const char *Name) {
  return LLVMAppendBasicBlockInContext(LLVMGetGlobalContext(), FnRef, Name);
}

LLVMBasicBlockRef LLVMInsertBasicBlockInContext(LLVMContextRef C,
                                                LLVMBasicBlockRef BBRef,
                                                const char *Name) {
  BasicBlock *BB = unwrap(BBRef);
  return wrap(BasicBlock::Create(*unwrap(C), Name, BB->getParent(), BB));
}

LLVMBasicBlockRef LLVMInsertBasicBlock(LLVMBasicBlockRef BBRef,
                                       const char *Name) {
  return LLVMInsertBasicBlockInContext(LLVMGetGlobalContext(), BBRef, Name);
}

void LLVMDeleteBasicBlock(LLVMBasicBlockRef BBRef) {
  unwrap(BBRef)->eraseFromParent();
}

void LLVMMoveBasicBlockBefore(LLVMBasicBlockRef BB, LLVMBasicBlockRef MovePos) {
  unwrap(BB)->moveBefore(unwrap(MovePos));
}

void LLVMMoveBasicBlockAfter(LLVMBasicBlockRef BB, LLVMBasicBlockRef MovePos) {
  unwrap(BB)->moveAfter(unwrap(MovePos));
}

/*--.. Operations on instructions ..........................................--*/

LLVMBasicBlockRef LLVMGetInstructionParent(LLVMValueRef Inst) {
  return wrap(unwrap<Instruction>(Inst)->getParent());
}

LLVMValueRef LLVMGetFirstInstruction(LLVMBasicBlockRef BB) {
  BasicBlock *Block = unwrap(BB);
  BasicBlock::iterator I = Block->begin();
  if (I == Block->end())
    return 0;
  return wrap(I);
}

LLVMValueRef LLVMGetLastInstruction(LLVMBasicBlockRef BB) {
  BasicBlock *Block = unwrap(BB);
  BasicBlock::iterator I = Block->end();
  if (I == Block->begin())
    return 0;
  return wrap(--I);
}

LLVMValueRef LLVMGetNextInstruction(LLVMValueRef Inst) {
  Instruction *Instr = unwrap<Instruction>(Inst);
  BasicBlock::iterator I = Instr;
  if (++I == Instr->getParent()->end())
    return 0;
  return wrap(I);
}

LLVMValueRef LLVMGetPreviousInstruction(LLVMValueRef Inst) {
  Instruction *Instr = unwrap<Instruction>(Inst);
  BasicBlock::iterator I = Instr;
  if (I == Instr->getParent()->begin())
    return 0;
  return wrap(--I);
}

/*--.. Call and invoke instructions ........................................--*/

unsigned LLVMGetInstructionCallConv(LLVMValueRef Instr) {
  Value *V = unwrap(Instr);
  if (CallInst *CI = dyn_cast<CallInst>(V))
    return CI->getCallingConv();
  else if (InvokeInst *II = dyn_cast<InvokeInst>(V))
    return II->getCallingConv();
  llvm_unreachable("LLVMGetInstructionCallConv applies only to call and invoke!");
  return 0;
}

void LLVMSetInstructionCallConv(LLVMValueRef Instr, unsigned CC) {
  Value *V = unwrap(Instr);
  if (CallInst *CI = dyn_cast<CallInst>(V))
    return CI->setCallingConv(static_cast<CallingConv::ID>(CC));
  else if (InvokeInst *II = dyn_cast<InvokeInst>(V))
    return II->setCallingConv(static_cast<CallingConv::ID>(CC));
  llvm_unreachable("LLVMSetInstructionCallConv applies only to call and invoke!");
}

void LLVMAddInstrAttribute(LLVMValueRef Instr, unsigned index, 
                           LLVMAttribute PA) {
  CallSite Call = CallSite(unwrap<Instruction>(Instr));
  Call.setAttributes(
    Call.getAttributes().addAttr(index, PA));
}

void LLVMRemoveInstrAttribute(LLVMValueRef Instr, unsigned index, 
                              LLVMAttribute PA) {
  CallSite Call = CallSite(unwrap<Instruction>(Instr));
  Call.setAttributes(
    Call.getAttributes().removeAttr(index, PA));
}

void LLVMSetInstrParamAlignment(LLVMValueRef Instr, unsigned index, 
                                unsigned align) {
  CallSite Call = CallSite(unwrap<Instruction>(Instr));
  Call.setAttributes(
    Call.getAttributes().addAttr(index, 
        Attribute::constructAlignmentFromInt(align)));
}

/*--.. Operations on call instructions (only) ..............................--*/

LLVMBool LLVMIsTailCall(LLVMValueRef Call) {
  return unwrap<CallInst>(Call)->isTailCall();
}

void LLVMSetTailCall(LLVMValueRef Call, LLVMBool isTailCall) {
  unwrap<CallInst>(Call)->setTailCall(isTailCall);
}

/*--.. Operations on phi nodes .............................................--*/

void LLVMAddIncoming(LLVMValueRef PhiNode, LLVMValueRef *IncomingValues,
                     LLVMBasicBlockRef *IncomingBlocks, unsigned Count) {
  PHINode *PhiVal = unwrap<PHINode>(PhiNode);
  for (unsigned I = 0; I != Count; ++I)
    PhiVal->addIncoming(unwrap(IncomingValues[I]), unwrap(IncomingBlocks[I]));
}

unsigned LLVMCountIncoming(LLVMValueRef PhiNode) {
  return unwrap<PHINode>(PhiNode)->getNumIncomingValues();
}

LLVMValueRef LLVMGetIncomingValue(LLVMValueRef PhiNode, unsigned Index) {
  return wrap(unwrap<PHINode>(PhiNode)->getIncomingValue(Index));
}

LLVMBasicBlockRef LLVMGetIncomingBlock(LLVMValueRef PhiNode, unsigned Index) {
  return wrap(unwrap<PHINode>(PhiNode)->getIncomingBlock(Index));
}


/*===-- Instruction builders ----------------------------------------------===*/

LLVMBuilderRef LLVMCreateBuilderInContext(LLVMContextRef C) {
  return wrap(new IRBuilder<>(*unwrap(C)));
}

LLVMBuilderRef LLVMCreateBuilder(void) {
  return LLVMCreateBuilderInContext(LLVMGetGlobalContext());
}

void LLVMPositionBuilder(LLVMBuilderRef Builder, LLVMBasicBlockRef Block,
                         LLVMValueRef Instr) {
  BasicBlock *BB = unwrap(Block);
  Instruction *I = Instr? unwrap<Instruction>(Instr) : (Instruction*) BB->end();
  unwrap(Builder)->SetInsertPoint(BB, I);
}

void LLVMPositionBuilderBefore(LLVMBuilderRef Builder, LLVMValueRef Instr) {
  Instruction *I = unwrap<Instruction>(Instr);
  unwrap(Builder)->SetInsertPoint(I->getParent(), I);
}

void LLVMPositionBuilderAtEnd(LLVMBuilderRef Builder, LLVMBasicBlockRef Block) {
  BasicBlock *BB = unwrap(Block);
  unwrap(Builder)->SetInsertPoint(BB);
}

LLVMBasicBlockRef LLVMGetInsertBlock(LLVMBuilderRef Builder) {
   return wrap(unwrap(Builder)->GetInsertBlock());
}

void LLVMClearInsertionPosition(LLVMBuilderRef Builder) {
  unwrap(Builder)->ClearInsertionPoint();
}

void LLVMInsertIntoBuilder(LLVMBuilderRef Builder, LLVMValueRef Instr) {
  unwrap(Builder)->Insert(unwrap<Instruction>(Instr));
}

void LLVMInsertIntoBuilderWithName(LLVMBuilderRef Builder, LLVMValueRef Instr,
                                   const char *Name) {
  unwrap(Builder)->Insert(unwrap<Instruction>(Instr), Name);
}

void LLVMDisposeBuilder(LLVMBuilderRef Builder) {
  delete unwrap(Builder);
}

/*--.. Metadata builders ...................................................--*/

void LLVMSetCurrentDebugLocation(LLVMBuilderRef Builder, LLVMValueRef L) {
  MDNode *Loc = L ? unwrap<MDNode>(L) : NULL;
  unwrap(Builder)->SetCurrentDebugLocation(DebugLoc::getFromDILocation(Loc));
}

LLVMValueRef LLVMGetCurrentDebugLocation(LLVMBuilderRef Builder) {
  return wrap(unwrap(Builder)->getCurrentDebugLocation()
              .getAsMDNode(unwrap(Builder)->getContext()));
}

void LLVMSetInstDebugLocation(LLVMBuilderRef Builder, LLVMValueRef Inst) {
  unwrap(Builder)->SetInstDebugLocation(unwrap<Instruction>(Inst));
}


/*--.. Instruction builders ................................................--*/

LLVMValueRef LLVMBuildRetVoid(LLVMBuilderRef B) {
  return wrap(unwrap(B)->CreateRetVoid());
}

LLVMValueRef LLVMBuildRet(LLVMBuilderRef B, LLVMValueRef V) {
  return wrap(unwrap(B)->CreateRet(unwrap(V)));
}

LLVMValueRef LLVMBuildAggregateRet(LLVMBuilderRef B, LLVMValueRef *RetVals,
                                   unsigned N) {
  return wrap(unwrap(B)->CreateAggregateRet(unwrap(RetVals), N));
}

LLVMValueRef LLVMBuildBr(LLVMBuilderRef B, LLVMBasicBlockRef Dest) {
  return wrap(unwrap(B)->CreateBr(unwrap(Dest)));
}

LLVMValueRef LLVMBuildCondBr(LLVMBuilderRef B, LLVMValueRef If,
                             LLVMBasicBlockRef Then, LLVMBasicBlockRef Else) {
  return wrap(unwrap(B)->CreateCondBr(unwrap(If), unwrap(Then), unwrap(Else)));
}

LLVMValueRef LLVMBuildSwitch(LLVMBuilderRef B, LLVMValueRef V,
                             LLVMBasicBlockRef Else, unsigned NumCases) {
  return wrap(unwrap(B)->CreateSwitch(unwrap(V), unwrap(Else), NumCases));
}

LLVMValueRef LLVMBuildIndirectBr(LLVMBuilderRef B, LLVMValueRef Addr,
                                 unsigned NumDests) {
  return wrap(unwrap(B)->CreateIndirectBr(unwrap(Addr), NumDests));
}

LLVMValueRef LLVMBuildInvoke(LLVMBuilderRef B, LLVMValueRef Fn,
                             LLVMValueRef *Args, unsigned NumArgs,
                             LLVMBasicBlockRef Then, LLVMBasicBlockRef Catch,
                             const char *Name) {
  return wrap(unwrap(B)->CreateInvoke(unwrap(Fn), unwrap(Then), unwrap(Catch),
                                      makeArrayRef(unwrap(Args), NumArgs),
                                      Name));
}

LLVMValueRef LLVMBuildResume(LLVMBuilderRef B, LLVMValueRef Exn) {
  return wrap(unwrap(B)->CreateResume(unwrap(Exn)));
}

LLVMValueRef LLVMBuildUnreachable(LLVMBuilderRef B) {
  return wrap(unwrap(B)->CreateUnreachable());
}

void LLVMAddCase(LLVMValueRef Switch, LLVMValueRef OnVal,
                 LLVMBasicBlockRef Dest) {
  unwrap<SwitchInst>(Switch)->addCase(unwrap<ConstantInt>(OnVal), unwrap(Dest));
}

void LLVMAddDestination(LLVMValueRef IndirectBr, LLVMBasicBlockRef Dest) {
  unwrap<IndirectBrInst>(IndirectBr)->addDestination(unwrap(Dest));
}

/*--.. Arithmetic ..........................................................--*/

LLVMValueRef LLVMBuildAdd(LLVMBuilderRef B, LLVMValueRef LHS, LLVMValueRef RHS,
                          const char *Name) {
  return wrap(unwrap(B)->CreateAdd(unwrap(LHS), unwrap(RHS), Name));
}

LLVMValueRef LLVMBuildNSWAdd(LLVMBuilderRef B, LLVMValueRef LHS, LLVMValueRef RHS,
                          const char *Name) {
  return wrap(unwrap(B)->CreateNSWAdd(unwrap(LHS), unwrap(RHS), Name));
}

LLVMValueRef LLVMBuildNUWAdd(LLVMBuilderRef B, LLVMValueRef LHS, LLVMValueRef RHS,
                          const char *Name) {
  return wrap(unwrap(B)->CreateNUWAdd(unwrap(LHS), unwrap(RHS), Name));
}

LLVMValueRef LLVMBuildFAdd(LLVMBuilderRef B, LLVMValueRef LHS, LLVMValueRef RHS,
                          const char *Name) {
  return wrap(unwrap(B)->CreateFAdd(unwrap(LHS), unwrap(RHS), Name));
}

LLVMValueRef LLVMBuildSub(LLVMBuilderRef B, LLVMValueRef LHS, LLVMValueRef RHS,
                          const char *Name) {
  return wrap(unwrap(B)->CreateSub(unwrap(LHS), unwrap(RHS), Name));
}

LLVMValueRef LLVMBuildNSWSub(LLVMBuilderRef B, LLVMValueRef LHS, LLVMValueRef RHS,
                          const char *Name) {
  return wrap(unwrap(B)->CreateNSWSub(unwrap(LHS), unwrap(RHS), Name));
}

LLVMValueRef LLVMBuildNUWSub(LLVMBuilderRef B, LLVMValueRef LHS, LLVMValueRef RHS,
                          const char *Name) {
  return wrap(unwrap(B)->CreateNUWSub(unwrap(LHS), unwrap(RHS), Name));
}

LLVMValueRef LLVMBuildFSub(LLVMBuilderRef B, LLVMValueRef LHS, LLVMValueRef RHS,
                          const char *Name) {
  return wrap(unwrap(B)->CreateFSub(unwrap(LHS), unwrap(RHS), Name));
}

LLVMValueRef LLVMBuildMul(LLVMBuilderRef B, LLVMValueRef LHS, LLVMValueRef RHS,
                          const char *Name) {
  return wrap(unwrap(B)->CreateMul(unwrap(LHS), unwrap(RHS), Name));
}

LLVMValueRef LLVMBuildNSWMul(LLVMBuilderRef B, LLVMValueRef LHS, LLVMValueRef RHS,
                          const char *Name) {
  return wrap(unwrap(B)->CreateNSWMul(unwrap(LHS), unwrap(RHS), Name));
}

LLVMValueRef LLVMBuildNUWMul(LLVMBuilderRef B, LLVMValueRef LHS, LLVMValueRef RHS,
                          const char *Name) {
  return wrap(unwrap(B)->CreateNUWMul(unwrap(LHS), unwrap(RHS), Name));
}

LLVMValueRef LLVMBuildFMul(LLVMBuilderRef B, LLVMValueRef LHS, LLVMValueRef RHS,
                          const char *Name) {
  return wrap(unwrap(B)->CreateFMul(unwrap(LHS), unwrap(RHS), Name));
}

LLVMValueRef LLVMBuildUDiv(LLVMBuilderRef B, LLVMValueRef LHS, LLVMValueRef RHS,
                           const char *Name) {
  return wrap(unwrap(B)->CreateUDiv(unwrap(LHS), unwrap(RHS), Name));
}

LLVMValueRef LLVMBuildSDiv(LLVMBuilderRef B, LLVMValueRef LHS, LLVMValueRef RHS,
                           const char *Name) {
  return wrap(unwrap(B)->CreateSDiv(unwrap(LHS), unwrap(RHS), Name));
}

LLVMValueRef LLVMBuildExactSDiv(LLVMBuilderRef B, LLVMValueRef LHS,
                                LLVMValueRef RHS, const char *Name) {
  return wrap(unwrap(B)->CreateExactSDiv(unwrap(LHS), unwrap(RHS), Name));
}

LLVMValueRef LLVMBuildFDiv(LLVMBuilderRef B, LLVMValueRef LHS, LLVMValueRef RHS,
                           const char *Name) {
  return wrap(unwrap(B)->CreateFDiv(unwrap(LHS), unwrap(RHS), Name));
}

LLVMValueRef LLVMBuildURem(LLVMBuilderRef B, LLVMValueRef LHS, LLVMValueRef RHS,
                           const char *Name) {
  return wrap(unwrap(B)->CreateURem(unwrap(LHS), unwrap(RHS), Name));
}

LLVMValueRef LLVMBuildSRem(LLVMBuilderRef B, LLVMValueRef LHS, LLVMValueRef RHS,
                           const char *Name) {
  return wrap(unwrap(B)->CreateSRem(unwrap(LHS), unwrap(RHS), Name));
}

LLVMValueRef LLVMBuildFRem(LLVMBuilderRef B, LLVMValueRef LHS, LLVMValueRef RHS,
                           const char *Name) {
  return wrap(unwrap(B)->CreateFRem(unwrap(LHS), unwrap(RHS), Name));
}

LLVMValueRef LLVMBuildShl(LLVMBuilderRef B, LLVMValueRef LHS, LLVMValueRef RHS,
                          const char *Name) {
  return wrap(unwrap(B)->CreateShl(unwrap(LHS), unwrap(RHS), Name));
}

LLVMValueRef LLVMBuildLShr(LLVMBuilderRef B, LLVMValueRef LHS, LLVMValueRef RHS,
                           const char *Name) {
  return wrap(unwrap(B)->CreateLShr(unwrap(LHS), unwrap(RHS), Name));
}

LLVMValueRef LLVMBuildAShr(LLVMBuilderRef B, LLVMValueRef LHS, LLVMValueRef RHS,
                           const char *Name) {
  return wrap(unwrap(B)->CreateAShr(unwrap(LHS), unwrap(RHS), Name));
}

LLVMValueRef LLVMBuildAnd(LLVMBuilderRef B, LLVMValueRef LHS, LLVMValueRef RHS,
                          const char *Name) {
  return wrap(unwrap(B)->CreateAnd(unwrap(LHS), unwrap(RHS), Name));
}

LLVMValueRef LLVMBuildOr(LLVMBuilderRef B, LLVMValueRef LHS, LLVMValueRef RHS,
                         const char *Name) {
  return wrap(unwrap(B)->CreateOr(unwrap(LHS), unwrap(RHS), Name));
}

LLVMValueRef LLVMBuildXor(LLVMBuilderRef B, LLVMValueRef LHS, LLVMValueRef RHS,
                          const char *Name) {
  return wrap(unwrap(B)->CreateXor(unwrap(LHS), unwrap(RHS), Name));
}

LLVMValueRef LLVMBuildBinOp(LLVMBuilderRef B, LLVMOpcode Op,
                            LLVMValueRef LHS, LLVMValueRef RHS,
                            const char *Name) {
  return wrap(unwrap(B)->CreateBinOp(Instruction::BinaryOps(Op), unwrap(LHS),
                                     unwrap(RHS), Name));
}

LLVMValueRef LLVMBuildNeg(LLVMBuilderRef B, LLVMValueRef V, const char *Name) {
  return wrap(unwrap(B)->CreateNeg(unwrap(V), Name));
}

LLVMValueRef LLVMBuildNSWNeg(LLVMBuilderRef B, LLVMValueRef V,
                             const char *Name) {
  return wrap(unwrap(B)->CreateNSWNeg(unwrap(V), Name));
}

LLVMValueRef LLVMBuildNUWNeg(LLVMBuilderRef B, LLVMValueRef V,
                             const char *Name) {
  return wrap(unwrap(B)->CreateNUWNeg(unwrap(V), Name));
}

LLVMValueRef LLVMBuildFNeg(LLVMBuilderRef B, LLVMValueRef V, const char *Name) {
  return wrap(unwrap(B)->CreateFNeg(unwrap(V), Name));
}

LLVMValueRef LLVMBuildNot(LLVMBuilderRef B, LLVMValueRef V, const char *Name) {
  return wrap(unwrap(B)->CreateNot(unwrap(V), Name));
}

/*--.. Memory ..............................................................--*/

LLVMValueRef LLVMBuildMalloc(LLVMBuilderRef B, LLVMTypeRef Ty,
                             const char *Name) {
  Type* ITy = Type::getInt32Ty(unwrap(B)->GetInsertBlock()->getContext());
  Constant* AllocSize = ConstantExpr::getSizeOf(unwrap(Ty));
  AllocSize = ConstantExpr::getTruncOrBitCast(AllocSize, ITy);
  Instruction* Malloc = CallInst::CreateMalloc(unwrap(B)->GetInsertBlock(), 
                                               ITy, unwrap(Ty), AllocSize, 
                                               0, 0, "");
  return wrap(unwrap(B)->Insert(Malloc, Twine(Name)));
}

LLVMValueRef LLVMBuildArrayMalloc(LLVMBuilderRef B, LLVMTypeRef Ty,
                                  LLVMValueRef Val, const char *Name) {
  Type* ITy = Type::getInt32Ty(unwrap(B)->GetInsertBlock()->getContext());
  Constant* AllocSize = ConstantExpr::getSizeOf(unwrap(Ty));
  AllocSize = ConstantExpr::getTruncOrBitCast(AllocSize, ITy);
  Instruction* Malloc = CallInst::CreateMalloc(unwrap(B)->GetInsertBlock(), 
                                               ITy, unwrap(Ty), AllocSize, 
                                               unwrap(Val), 0, "");
  return wrap(unwrap(B)->Insert(Malloc, Twine(Name)));
}

LLVMValueRef LLVMBuildAlloca(LLVMBuilderRef B, LLVMTypeRef Ty,
                             const char *Name) {
  return wrap(unwrap(B)->CreateAlloca(unwrap(Ty), 0, Name));
}

LLVMValueRef LLVMBuildArrayAlloca(LLVMBuilderRef B, LLVMTypeRef Ty,
                                  LLVMValueRef Val, const char *Name) {
  return wrap(unwrap(B)->CreateAlloca(unwrap(Ty), unwrap(Val), Name));
}

LLVMValueRef LLVMBuildFree(LLVMBuilderRef B, LLVMValueRef PointerVal) {
  return wrap(unwrap(B)->Insert(
     CallInst::CreateFree(unwrap(PointerVal), unwrap(B)->GetInsertBlock())));
}


LLVMValueRef LLVMBuildLoad(LLVMBuilderRef B, LLVMValueRef PointerVal,
                           const char *Name) {
  return wrap(unwrap(B)->CreateLoad(unwrap(PointerVal), Name));
}

LLVMValueRef LLVMBuildStore(LLVMBuilderRef B, LLVMValueRef Val, 
                            LLVMValueRef PointerVal) {
  return wrap(unwrap(B)->CreateStore(unwrap(Val), unwrap(PointerVal)));
}

LLVMValueRef LLVMBuildGEP(LLVMBuilderRef B, LLVMValueRef Pointer,
                          LLVMValueRef *Indices, unsigned NumIndices,
                          const char *Name) {
  ArrayRef<Value *> IdxList(unwrap(Indices), NumIndices);
  return wrap(unwrap(B)->CreateGEP(unwrap(Pointer), IdxList, Name));
}

LLVMValueRef LLVMBuildInBoundsGEP(LLVMBuilderRef B, LLVMValueRef Pointer,
                                  LLVMValueRef *Indices, unsigned NumIndices,
                                  const char *Name) {
  ArrayRef<Value *> IdxList(unwrap(Indices), NumIndices);
  return wrap(unwrap(B)->CreateInBoundsGEP(unwrap(Pointer), IdxList, Name));
}

LLVMValueRef LLVMBuildStructGEP(LLVMBuilderRef B, LLVMValueRef Pointer,
                                unsigned Idx, const char *Name) {
  return wrap(unwrap(B)->CreateStructGEP(unwrap(Pointer), Idx, Name));
}

LLVMValueRef LLVMBuildGlobalString(LLVMBuilderRef B, const char *Str,
                                   const char *Name) {
  return wrap(unwrap(B)->CreateGlobalString(Str, Name));
}

LLVMValueRef LLVMBuildGlobalStringPtr(LLVMBuilderRef B, const char *Str,
                                      const char *Name) {
  return wrap(unwrap(B)->CreateGlobalStringPtr(Str, Name));
}

/*--.. Casts ...............................................................--*/

LLVMValueRef LLVMBuildTrunc(LLVMBuilderRef B, LLVMValueRef Val,
                            LLVMTypeRef DestTy, const char *Name) {
  return wrap(unwrap(B)->CreateTrunc(unwrap(Val), unwrap(DestTy), Name));
}

LLVMValueRef LLVMBuildZExt(LLVMBuilderRef B, LLVMValueRef Val,
                           LLVMTypeRef DestTy, const char *Name) {
  return wrap(unwrap(B)->CreateZExt(unwrap(Val), unwrap(DestTy), Name));
}

LLVMValueRef LLVMBuildSExt(LLVMBuilderRef B, LLVMValueRef Val,
                           LLVMTypeRef DestTy, const char *Name) {
  return wrap(unwrap(B)->CreateSExt(unwrap(Val), unwrap(DestTy), Name));
}

LLVMValueRef LLVMBuildFPToUI(LLVMBuilderRef B, LLVMValueRef Val,
                             LLVMTypeRef DestTy, const char *Name) {
  return wrap(unwrap(B)->CreateFPToUI(unwrap(Val), unwrap(DestTy), Name));
}

LLVMValueRef LLVMBuildFPToSI(LLVMBuilderRef B, LLVMValueRef Val,
                             LLVMTypeRef DestTy, const char *Name) {
  return wrap(unwrap(B)->CreateFPToSI(unwrap(Val), unwrap(DestTy), Name));
}

LLVMValueRef LLVMBuildUIToFP(LLVMBuilderRef B, LLVMValueRef Val,
                             LLVMTypeRef DestTy, const char *Name) {
  return wrap(unwrap(B)->CreateUIToFP(unwrap(Val), unwrap(DestTy), Name));
}

LLVMValueRef LLVMBuildSIToFP(LLVMBuilderRef B, LLVMValueRef Val,
                             LLVMTypeRef DestTy, const char *Name) {
  return wrap(unwrap(B)->CreateSIToFP(unwrap(Val), unwrap(DestTy), Name));
}

LLVMValueRef LLVMBuildFPTrunc(LLVMBuilderRef B, LLVMValueRef Val,
                              LLVMTypeRef DestTy, const char *Name) {
  return wrap(unwrap(B)->CreateFPTrunc(unwrap(Val), unwrap(DestTy), Name));
}

LLVMValueRef LLVMBuildFPExt(LLVMBuilderRef B, LLVMValueRef Val,
                            LLVMTypeRef DestTy, const char *Name) {
  return wrap(unwrap(B)->CreateFPExt(unwrap(Val), unwrap(DestTy), Name));
}

LLVMValueRef LLVMBuildPtrToInt(LLVMBuilderRef B, LLVMValueRef Val,
                               LLVMTypeRef DestTy, const char *Name) {
  return wrap(unwrap(B)->CreatePtrToInt(unwrap(Val), unwrap(DestTy), Name));
}

LLVMValueRef LLVMBuildIntToPtr(LLVMBuilderRef B, LLVMValueRef Val,
                               LLVMTypeRef DestTy, const char *Name) {
  return wrap(unwrap(B)->CreateIntToPtr(unwrap(Val), unwrap(DestTy), Name));
}

LLVMValueRef LLVMBuildBitCast(LLVMBuilderRef B, LLVMValueRef Val,
                              LLVMTypeRef DestTy, const char *Name) {
  return wrap(unwrap(B)->CreateBitCast(unwrap(Val), unwrap(DestTy), Name));
}

LLVMValueRef LLVMBuildZExtOrBitCast(LLVMBuilderRef B, LLVMValueRef Val,
                                    LLVMTypeRef DestTy, const char *Name) {
  return wrap(unwrap(B)->CreateZExtOrBitCast(unwrap(Val), unwrap(DestTy),
                                             Name));
}

LLVMValueRef LLVMBuildSExtOrBitCast(LLVMBuilderRef B, LLVMValueRef Val,
                                    LLVMTypeRef DestTy, const char *Name) {
  return wrap(unwrap(B)->CreateSExtOrBitCast(unwrap(Val), unwrap(DestTy),
                                             Name));
}

LLVMValueRef LLVMBuildTruncOrBitCast(LLVMBuilderRef B, LLVMValueRef Val,
                                     LLVMTypeRef DestTy, const char *Name) {
  return wrap(unwrap(B)->CreateTruncOrBitCast(unwrap(Val), unwrap(DestTy),
                                              Name));
}

LLVMValueRef LLVMBuildCast(LLVMBuilderRef B, LLVMOpcode Op, LLVMValueRef Val,
                           LLVMTypeRef DestTy, const char *Name) {
  return wrap(unwrap(B)->CreateCast(Instruction::CastOps(Op), unwrap(Val),
                                    unwrap(DestTy), Name));
}

LLVMValueRef LLVMBuildPointerCast(LLVMBuilderRef B, LLVMValueRef Val,
                                  LLVMTypeRef DestTy, const char *Name) {
  return wrap(unwrap(B)->CreatePointerCast(unwrap(Val), unwrap(DestTy), Name));
}

LLVMValueRef LLVMBuildIntCast(LLVMBuilderRef B, LLVMValueRef Val,
                              LLVMTypeRef DestTy, const char *Name) {
  return wrap(unwrap(B)->CreateIntCast(unwrap(Val), unwrap(DestTy),
                                       /*isSigned*/true, Name));
}

LLVMValueRef LLVMBuildFPCast(LLVMBuilderRef B, LLVMValueRef Val,
                             LLVMTypeRef DestTy, const char *Name) {
  return wrap(unwrap(B)->CreateFPCast(unwrap(Val), unwrap(DestTy), Name));
}

/*--.. Comparisons .........................................................--*/

LLVMValueRef LLVMBuildICmp(LLVMBuilderRef B, LLVMIntPredicate Op,
                           LLVMValueRef LHS, LLVMValueRef RHS,
                           const char *Name) {
  return wrap(unwrap(B)->CreateICmp(static_cast<ICmpInst::Predicate>(Op),
                                    unwrap(LHS), unwrap(RHS), Name));
}

LLVMValueRef LLVMBuildFCmp(LLVMBuilderRef B, LLVMRealPredicate Op,
                           LLVMValueRef LHS, LLVMValueRef RHS,
                           const char *Name) {
  return wrap(unwrap(B)->CreateFCmp(static_cast<FCmpInst::Predicate>(Op),
                                    unwrap(LHS), unwrap(RHS), Name));
}

/*--.. Miscellaneous instructions ..........................................--*/

LLVMValueRef LLVMBuildPhi(LLVMBuilderRef B, LLVMTypeRef Ty, const char *Name) {
  return wrap(unwrap(B)->CreatePHI(unwrap(Ty), 0, Name));
}

LLVMValueRef LLVMBuildCall(LLVMBuilderRef B, LLVMValueRef Fn,
                           LLVMValueRef *Args, unsigned NumArgs,
                           const char *Name) {
  return wrap(unwrap(B)->CreateCall(unwrap(Fn),
                                    makeArrayRef(unwrap(Args), NumArgs),
                                    Name));
}

LLVMValueRef LLVMBuildSelect(LLVMBuilderRef B, LLVMValueRef If,
                             LLVMValueRef Then, LLVMValueRef Else,
                             const char *Name) {
  return wrap(unwrap(B)->CreateSelect(unwrap(If), unwrap(Then), unwrap(Else),
                                      Name));
}

LLVMValueRef LLVMBuildVAArg(LLVMBuilderRef B, LLVMValueRef List,
                            LLVMTypeRef Ty, const char *Name) {
  return wrap(unwrap(B)->CreateVAArg(unwrap(List), unwrap(Ty), Name));
}

LLVMValueRef LLVMBuildExtractElement(LLVMBuilderRef B, LLVMValueRef VecVal,
                                      LLVMValueRef Index, const char *Name) {
  return wrap(unwrap(B)->CreateExtractElement(unwrap(VecVal), unwrap(Index),
                                              Name));
}

LLVMValueRef LLVMBuildInsertElement(LLVMBuilderRef B, LLVMValueRef VecVal,
                                    LLVMValueRef EltVal, LLVMValueRef Index,
                                    const char *Name) {
  return wrap(unwrap(B)->CreateInsertElement(unwrap(VecVal), unwrap(EltVal),
                                             unwrap(Index), Name));
}

LLVMValueRef LLVMBuildShuffleVector(LLVMBuilderRef B, LLVMValueRef V1,
                                    LLVMValueRef V2, LLVMValueRef Mask,
                                    const char *Name) {
  return wrap(unwrap(B)->CreateShuffleVector(unwrap(V1), unwrap(V2),
                                             unwrap(Mask), Name));
}

LLVMValueRef LLVMBuildExtractValue(LLVMBuilderRef B, LLVMValueRef AggVal,
                                   unsigned Index, const char *Name) {
  return wrap(unwrap(B)->CreateExtractValue(unwrap(AggVal), Index, Name));
}

LLVMValueRef LLVMBuildInsertValue(LLVMBuilderRef B, LLVMValueRef AggVal,
                                  LLVMValueRef EltVal, unsigned Index,
                                  const char *Name) {
  return wrap(unwrap(B)->CreateInsertValue(unwrap(AggVal), unwrap(EltVal),
                                           Index, Name));
}

LLVMValueRef LLVMBuildIsNull(LLVMBuilderRef B, LLVMValueRef Val,
                             const char *Name) {
  return wrap(unwrap(B)->CreateIsNull(unwrap(Val), Name));
}

LLVMValueRef LLVMBuildIsNotNull(LLVMBuilderRef B, LLVMValueRef Val,
                                const char *Name) {
  return wrap(unwrap(B)->CreateIsNotNull(unwrap(Val), Name));
}

LLVMValueRef LLVMBuildPtrDiff(LLVMBuilderRef B, LLVMValueRef LHS,
                              LLVMValueRef RHS, const char *Name) {
  return wrap(unwrap(B)->CreatePtrDiff(unwrap(LHS), unwrap(RHS), Name));
}


/*===-- Module providers --------------------------------------------------===*/

LLVMModuleProviderRef
LLVMCreateModuleProviderForExistingModule(LLVMModuleRef M) {
  return reinterpret_cast<LLVMModuleProviderRef>(M);
}

void LLVMDisposeModuleProvider(LLVMModuleProviderRef MP) {
  delete unwrap(MP);
}


/*===-- Memory buffers ----------------------------------------------------===*/

LLVMBool LLVMCreateMemoryBufferWithContentsOfFile(
    const char *Path,
    LLVMMemoryBufferRef *OutMemBuf,
    char **OutMessage) {

  OwningPtr<MemoryBuffer> MB;
  error_code ec;
  if (!(ec = MemoryBuffer::getFile(Path, MB))) {
    *OutMemBuf = wrap(MB.take());
    return 0;
  }

  *OutMessage = strdup(ec.message().c_str());
  return 1;
}

LLVMBool LLVMCreateMemoryBufferWithSTDIN(LLVMMemoryBufferRef *OutMemBuf,
                                         char **OutMessage) {
  OwningPtr<MemoryBuffer> MB;
  error_code ec;
  if (!(ec = MemoryBuffer::getSTDIN(MB))) {
    *OutMemBuf = wrap(MB.take());
    return 0;
  }

  *OutMessage = strdup(ec.message().c_str());
  return 1;
}

void LLVMDisposeMemoryBuffer(LLVMMemoryBufferRef MemBuf) {
  delete unwrap(MemBuf);
}

/*===-- Pass Registry -----------------------------------------------------===*/

LLVMPassRegistryRef LLVMGetGlobalPassRegistry(void) {
  return wrap(PassRegistry::getPassRegistry());
}

/*===-- Pass Manager ------------------------------------------------------===*/

LLVMPassManagerRef LLVMCreatePassManager() {
  return wrap(new PassManager());
}

LLVMPassManagerRef LLVMCreateFunctionPassManagerForModule(LLVMModuleRef M) {
  return wrap(new FunctionPassManager(unwrap(M)));
}

LLVMPassManagerRef LLVMCreateFunctionPassManager(LLVMModuleProviderRef P) {
  return LLVMCreateFunctionPassManagerForModule(
                                            reinterpret_cast<LLVMModuleRef>(P));
}

LLVMBool LLVMRunPassManager(LLVMPassManagerRef PM, LLVMModuleRef M) {
  return unwrap<PassManager>(PM)->run(*unwrap(M));
}

LLVMBool LLVMInitializeFunctionPassManager(LLVMPassManagerRef FPM) {
  return unwrap<FunctionPassManager>(FPM)->doInitialization();
}

LLVMBool LLVMRunFunctionPassManager(LLVMPassManagerRef FPM, LLVMValueRef F) {
  return unwrap<FunctionPassManager>(FPM)->run(*unwrap<Function>(F));
}

LLVMBool LLVMFinalizeFunctionPassManager(LLVMPassManagerRef FPM) {
  return unwrap<FunctionPassManager>(FPM)->doFinalization();
}

void LLVMDisposePassManager(LLVMPassManagerRef PM) {
  delete unwrap(PM);
}
