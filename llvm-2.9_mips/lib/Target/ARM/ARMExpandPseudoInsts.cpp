//===-- ARMExpandPseudoInsts.cpp - Expand pseudo instructions -----*- C++ -*-=//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains a pass that expands pseudo instructions into target
// instructions to allow proper scheduling, if-conversion, and other late
// optimizations. This pass should be run after register allocation but before
// the post-regalloc scheduling pass.
//
//===----------------------------------------------------------------------===//

#define DEBUG_TYPE "arm-pseudo"
#include "ARM.h"
#include "ARMBaseInstrInfo.h"
#include "ARMBaseRegisterInfo.h"
#include "ARMMachineFunctionInfo.h"
#include "ARMRegisterInfo.h"
#include "MCTargetDesc/ARMAddressingModes.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/Target/TargetFrameLowering.h"
#include "llvm/Target/TargetRegisterInfo.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/raw_ostream.h" // FIXME: for debug only. remove!
using namespace llvm;

cl::opt<bool>
VerifyARMPseudo("verify-arm-pseudo-expand", cl::Hidden,
                cl::desc("Verify machine code after expanding ARM pseudos"));

namespace {
  class ARMExpandPseudo : public MachineFunctionPass {
  public:
    static char ID;
    ARMExpandPseudo() : MachineFunctionPass(ID) {}

    const ARMBaseInstrInfo *TII;
    const TargetRegisterInfo *TRI;
    const ARMSubtarget *STI;
    ARMFunctionInfo *AFI;

    virtual bool runOnMachineFunction(MachineFunction &Fn);

    virtual const char *getPassName() const {
      return "ARM pseudo instruction expansion pass";
    }

  private:
    void TransferImpOps(MachineInstr &OldMI,
                        MachineInstrBuilder &UseMI, MachineInstrBuilder &DefMI);
    bool ExpandMI(MachineBasicBlock &MBB,
                  MachineBasicBlock::iterator MBBI);
    bool ExpandMBB(MachineBasicBlock &MBB);
    void ExpandVLD(MachineBasicBlock::iterator &MBBI);
    void ExpandVST(MachineBasicBlock::iterator &MBBI);
    void ExpandLaneOp(MachineBasicBlock::iterator &MBBI);
    void ExpandVTBL(MachineBasicBlock::iterator &MBBI,
                    unsigned Opc, bool IsExt, unsigned NumRegs);
    void ExpandMOV32BitImm(MachineBasicBlock &MBB,
                           MachineBasicBlock::iterator &MBBI);
  };
  char ARMExpandPseudo::ID = 0;
}

/// TransferImpOps - Transfer implicit operands on the pseudo instruction to
/// the instructions created from the expansion.
void ARMExpandPseudo::TransferImpOps(MachineInstr &OldMI,
                                     MachineInstrBuilder &UseMI,
                                     MachineInstrBuilder &DefMI) {
  const MCInstrDesc &Desc = OldMI.getDesc();
  for (unsigned i = Desc.getNumOperands(), e = OldMI.getNumOperands();
       i != e; ++i) {
    const MachineOperand &MO = OldMI.getOperand(i);
    assert(MO.isReg() && MO.getReg());
    if (MO.isUse())
      UseMI.addOperand(MO);
    else
      DefMI.addOperand(MO);
  }
}

namespace {
  // Constants for register spacing in NEON load/store instructions.
  // For quad-register load-lane and store-lane pseudo instructors, the
  // spacing is initially assumed to be EvenDblSpc, and that is changed to
  // OddDblSpc depending on the lane number operand.
  enum NEONRegSpacing {
    SingleSpc,
    EvenDblSpc,
    OddDblSpc
  };

  // Entries for NEON load/store information table.  The table is sorted by
  // PseudoOpc for fast binary-search lookups.
  struct NEONLdStTableEntry {
    unsigned PseudoOpc;
    unsigned RealOpc;
    bool IsLoad;
    bool HasWriteBack;
    NEONRegSpacing RegSpacing;
    unsigned char NumRegs; // D registers loaded or stored
    unsigned char RegElts; // elements per D register; used for lane ops

    // Comparison methods for binary search of the table.
    bool operator<(const NEONLdStTableEntry &TE) const {
      return PseudoOpc < TE.PseudoOpc;
    }
    friend bool operator<(const NEONLdStTableEntry &TE, unsigned PseudoOpc) {
      return TE.PseudoOpc < PseudoOpc;
    }
    friend bool LLVM_ATTRIBUTE_UNUSED operator<(unsigned PseudoOpc,
                                                const NEONLdStTableEntry &TE) {
      return PseudoOpc < TE.PseudoOpc;
    }
  };
}

static const NEONLdStTableEntry NEONLdStTable[] = {
{ ARM::VLD1DUPq16Pseudo,     ARM::VLD1DUPq16,     true, false, SingleSpc, 2, 4},
{ ARM::VLD1DUPq16Pseudo_UPD, ARM::VLD1DUPq16_UPD, true, true,  SingleSpc, 2, 4},
{ ARM::VLD1DUPq32Pseudo,     ARM::VLD1DUPq32,     true, false, SingleSpc, 2, 2},
{ ARM::VLD1DUPq32Pseudo_UPD, ARM::VLD1DUPq32_UPD, true, true,  SingleSpc, 2, 2},
{ ARM::VLD1DUPq8Pseudo,      ARM::VLD1DUPq8,      true, false, SingleSpc, 2, 8},
{ ARM::VLD1DUPq8Pseudo_UPD,  ARM::VLD1DUPq8_UPD,  true, true,  SingleSpc, 2, 8},

{ ARM::VLD1LNq16Pseudo,     ARM::VLD1LNd16,     true, false, EvenDblSpc, 1, 4 },
{ ARM::VLD1LNq16Pseudo_UPD, ARM::VLD1LNd16_UPD, true, true,  EvenDblSpc, 1, 4 },
{ ARM::VLD1LNq32Pseudo,     ARM::VLD1LNd32,     true, false, EvenDblSpc, 1, 2 },
{ ARM::VLD1LNq32Pseudo_UPD, ARM::VLD1LNd32_UPD, true, true,  EvenDblSpc, 1, 2 },
{ ARM::VLD1LNq8Pseudo,      ARM::VLD1LNd8,      true, false, EvenDblSpc, 1, 8 },
{ ARM::VLD1LNq8Pseudo_UPD,  ARM::VLD1LNd8_UPD,  true, true,  EvenDblSpc, 1, 8 },

{ ARM::VLD1d64QPseudo,      ARM::VLD1d64Q,     true,  false, SingleSpc,  4, 1 },
{ ARM::VLD1d64QPseudo_UPD,  ARM::VLD1d64Q_UPD, true,  true,  SingleSpc,  4, 1 },
{ ARM::VLD1d64TPseudo,      ARM::VLD1d64T,     true,  false, SingleSpc,  3, 1 },
{ ARM::VLD1d64TPseudo_UPD,  ARM::VLD1d64T_UPD, true,  true,  SingleSpc,  3, 1 },

{ ARM::VLD1q16Pseudo,       ARM::VLD1q16,      true,  false, SingleSpc,  2, 4 },
{ ARM::VLD1q16Pseudo_UPD,   ARM::VLD1q16_UPD,  true,  true,  SingleSpc,  2, 4 },
{ ARM::VLD1q32Pseudo,       ARM::VLD1q32,      true,  false, SingleSpc,  2, 2 },
{ ARM::VLD1q32Pseudo_UPD,   ARM::VLD1q32_UPD,  true,  true,  SingleSpc,  2, 2 },
{ ARM::VLD1q64Pseudo,       ARM::VLD1q64,      true,  false, SingleSpc,  2, 1 },
{ ARM::VLD1q64Pseudo_UPD,   ARM::VLD1q64_UPD,  true,  true,  SingleSpc,  2, 1 },
{ ARM::VLD1q8Pseudo,        ARM::VLD1q8,       true,  false, SingleSpc,  2, 8 },
{ ARM::VLD1q8Pseudo_UPD,    ARM::VLD1q8_UPD,   true,  true,  SingleSpc,  2, 8 },

{ ARM::VLD2DUPd16Pseudo,     ARM::VLD2DUPd16,     true, false, SingleSpc, 2, 4},
{ ARM::VLD2DUPd16Pseudo_UPD, ARM::VLD2DUPd16_UPD, true, true,  SingleSpc, 2, 4},
{ ARM::VLD2DUPd32Pseudo,     ARM::VLD2DUPd32,     true, false, SingleSpc, 2, 2},
{ ARM::VLD2DUPd32Pseudo_UPD, ARM::VLD2DUPd32_UPD, true, true,  SingleSpc, 2, 2},
{ ARM::VLD2DUPd8Pseudo,      ARM::VLD2DUPd8,      true, false, SingleSpc, 2, 8},
{ ARM::VLD2DUPd8Pseudo_UPD,  ARM::VLD2DUPd8_UPD,  true, true,  SingleSpc, 2, 8},

{ ARM::VLD2LNd16Pseudo,     ARM::VLD2LNd16,     true, false, SingleSpc,  2, 4 },
{ ARM::VLD2LNd16Pseudo_UPD, ARM::VLD2LNd16_UPD, true, true,  SingleSpc,  2, 4 },
{ ARM::VLD2LNd32Pseudo,     ARM::VLD2LNd32,     true, false, SingleSpc,  2, 2 },
{ ARM::VLD2LNd32Pseudo_UPD, ARM::VLD2LNd32_UPD, true, true,  SingleSpc,  2, 2 },
{ ARM::VLD2LNd8Pseudo,      ARM::VLD2LNd8,      true, false, SingleSpc,  2, 8 },
{ ARM::VLD2LNd8Pseudo_UPD,  ARM::VLD2LNd8_UPD,  true, true,  SingleSpc,  2, 8 },
{ ARM::VLD2LNq16Pseudo,     ARM::VLD2LNq16,     true, false, EvenDblSpc, 2, 4 },
{ ARM::VLD2LNq16Pseudo_UPD, ARM::VLD2LNq16_UPD, true, true,  EvenDblSpc, 2, 4 },
{ ARM::VLD2LNq32Pseudo,     ARM::VLD2LNq32,     true, false, EvenDblSpc, 2, 2 },
{ ARM::VLD2LNq32Pseudo_UPD, ARM::VLD2LNq32_UPD, true, true,  EvenDblSpc, 2, 2 },

{ ARM::VLD2d16Pseudo,       ARM::VLD2d16,      true,  false, SingleSpc,  2, 4 },
{ ARM::VLD2d16Pseudo_UPD,   ARM::VLD2d16_UPD,  true,  true,  SingleSpc,  2, 4 },
{ ARM::VLD2d32Pseudo,       ARM::VLD2d32,      true,  false, SingleSpc,  2, 2 },
{ ARM::VLD2d32Pseudo_UPD,   ARM::VLD2d32_UPD,  true,  true,  SingleSpc,  2, 2 },
{ ARM::VLD2d8Pseudo,        ARM::VLD2d8,       true,  false, SingleSpc,  2, 8 },
{ ARM::VLD2d8Pseudo_UPD,    ARM::VLD2d8_UPD,   true,  true,  SingleSpc,  2, 8 },

{ ARM::VLD2q16Pseudo,       ARM::VLD2q16,      true,  false, SingleSpc,  4, 4 },
{ ARM::VLD2q16Pseudo_UPD,   ARM::VLD2q16_UPD,  true,  true,  SingleSpc,  4, 4 },
{ ARM::VLD2q32Pseudo,       ARM::VLD2q32,      true,  false, SingleSpc,  4, 2 },
{ ARM::VLD2q32Pseudo_UPD,   ARM::VLD2q32_UPD,  true,  true,  SingleSpc,  4, 2 },
{ ARM::VLD2q8Pseudo,        ARM::VLD2q8,       true,  false, SingleSpc,  4, 8 },
{ ARM::VLD2q8Pseudo_UPD,    ARM::VLD2q8_UPD,   true,  true,  SingleSpc,  4, 8 },

{ ARM::VLD3DUPd16Pseudo,     ARM::VLD3DUPd16,     true, false, SingleSpc, 3, 4},
{ ARM::VLD3DUPd16Pseudo_UPD, ARM::VLD3DUPd16_UPD, true, true,  SingleSpc, 3, 4},
{ ARM::VLD3DUPd32Pseudo,     ARM::VLD3DUPd32,     true, false, SingleSpc, 3, 2},
{ ARM::VLD3DUPd32Pseudo_UPD, ARM::VLD3DUPd32_UPD, true, true,  SingleSpc, 3, 2},
{ ARM::VLD3DUPd8Pseudo,      ARM::VLD3DUPd8,      true, false, SingleSpc, 3, 8},
{ ARM::VLD3DUPd8Pseudo_UPD,  ARM::VLD3DUPd8_UPD,  true, true,  SingleSpc, 3, 8},

{ ARM::VLD3LNd16Pseudo,     ARM::VLD3LNd16,     true, false, SingleSpc,  3, 4 },
{ ARM::VLD3LNd16Pseudo_UPD, ARM::VLD3LNd16_UPD, true, true,  SingleSpc,  3, 4 },
{ ARM::VLD3LNd32Pseudo,     ARM::VLD3LNd32,     true, false, SingleSpc,  3, 2 },
{ ARM::VLD3LNd32Pseudo_UPD, ARM::VLD3LNd32_UPD, true, true,  SingleSpc,  3, 2 },
{ ARM::VLD3LNd8Pseudo,      ARM::VLD3LNd8,      true, false, SingleSpc,  3, 8 },
{ ARM::VLD3LNd8Pseudo_UPD,  ARM::VLD3LNd8_UPD,  true, true,  SingleSpc,  3, 8 },
{ ARM::VLD3LNq16Pseudo,     ARM::VLD3LNq16,     true, false, EvenDblSpc, 3, 4 },
{ ARM::VLD3LNq16Pseudo_UPD, ARM::VLD3LNq16_UPD, true, true,  EvenDblSpc, 3, 4 },
{ ARM::VLD3LNq32Pseudo,     ARM::VLD3LNq32,     true, false, EvenDblSpc, 3, 2 },
{ ARM::VLD3LNq32Pseudo_UPD, ARM::VLD3LNq32_UPD, true, true,  EvenDblSpc, 3, 2 },

{ ARM::VLD3d16Pseudo,       ARM::VLD3d16,      true,  false, SingleSpc,  3, 4 },
{ ARM::VLD3d16Pseudo_UPD,   ARM::VLD3d16_UPD,  true,  true,  SingleSpc,  3, 4 },
{ ARM::VLD3d32Pseudo,       ARM::VLD3d32,      true,  false, SingleSpc,  3, 2 },
{ ARM::VLD3d32Pseudo_UPD,   ARM::VLD3d32_UPD,  true,  true,  SingleSpc,  3, 2 },
{ ARM::VLD3d8Pseudo,        ARM::VLD3d8,       true,  false, SingleSpc,  3, 8 },
{ ARM::VLD3d8Pseudo_UPD,    ARM::VLD3d8_UPD,   true,  true,  SingleSpc,  3, 8 },

{ ARM::VLD3q16Pseudo_UPD,    ARM::VLD3q16_UPD, true,  true,  EvenDblSpc, 3, 4 },
{ ARM::VLD3q16oddPseudo,     ARM::VLD3q16,     true,  false, OddDblSpc,  3, 4 },
{ ARM::VLD3q16oddPseudo_UPD, ARM::VLD3q16_UPD, true,  true,  OddDblSpc,  3, 4 },
{ ARM::VLD3q32Pseudo_UPD,    ARM::VLD3q32_UPD, true,  true,  EvenDblSpc, 3, 2 },
{ ARM::VLD3q32oddPseudo,     ARM::VLD3q32,     true,  false, OddDblSpc,  3, 2 },
{ ARM::VLD3q32oddPseudo_UPD, ARM::VLD3q32_UPD, true,  true,  OddDblSpc,  3, 2 },
{ ARM::VLD3q8Pseudo_UPD,     ARM::VLD3q8_UPD,  true,  true,  EvenDblSpc, 3, 8 },
{ ARM::VLD3q8oddPseudo,      ARM::VLD3q8,      true,  false, OddDblSpc,  3, 8 },
{ ARM::VLD3q8oddPseudo_UPD,  ARM::VLD3q8_UPD,  true,  true,  OddDblSpc,  3, 8 },

{ ARM::VLD4DUPd16Pseudo,     ARM::VLD4DUPd16,     true, false, SingleSpc, 4, 4},
{ ARM::VLD4DUPd16Pseudo_UPD, ARM::VLD4DUPd16_UPD, true, true,  SingleSpc, 4, 4},
{ ARM::VLD4DUPd32Pseudo,     ARM::VLD4DUPd32,     true, false, SingleSpc, 4, 2},
{ ARM::VLD4DUPd32Pseudo_UPD, ARM::VLD4DUPd32_UPD, true, true,  SingleSpc, 4, 2},
{ ARM::VLD4DUPd8Pseudo,      ARM::VLD4DUPd8,      true, false, SingleSpc, 4, 8},
{ ARM::VLD4DUPd8Pseudo_UPD,  ARM::VLD4DUPd8_UPD,  true, true,  SingleSpc, 4, 8},

{ ARM::VLD4LNd16Pseudo,     ARM::VLD4LNd16,     true, false, SingleSpc,  4, 4 },
{ ARM::VLD4LNd16Pseudo_UPD, ARM::VLD4LNd16_UPD, true, true,  SingleSpc,  4, 4 },
{ ARM::VLD4LNd32Pseudo,     ARM::VLD4LNd32,     true, false, SingleSpc,  4, 2 },
{ ARM::VLD4LNd32Pseudo_UPD, ARM::VLD4LNd32_UPD, true, true,  SingleSpc,  4, 2 },
{ ARM::VLD4LNd8Pseudo,      ARM::VLD4LNd8,      true, false, SingleSpc,  4, 8 },
{ ARM::VLD4LNd8Pseudo_UPD,  ARM::VLD4LNd8_UPD,  true, true,  SingleSpc,  4, 8 },
{ ARM::VLD4LNq16Pseudo,     ARM::VLD4LNq16,     true, false, EvenDblSpc, 4, 4 },
{ ARM::VLD4LNq16Pseudo_UPD, ARM::VLD4LNq16_UPD, true, true,  EvenDblSpc, 4, 4 },
{ ARM::VLD4LNq32Pseudo,     ARM::VLD4LNq32,     true, false, EvenDblSpc, 4, 2 },
{ ARM::VLD4LNq32Pseudo_UPD, ARM::VLD4LNq32_UPD, true, true,  EvenDblSpc, 4, 2 },

{ ARM::VLD4d16Pseudo,       ARM::VLD4d16,      true,  false, SingleSpc,  4, 4 },
{ ARM::VLD4d16Pseudo_UPD,   ARM::VLD4d16_UPD,  true,  true,  SingleSpc,  4, 4 },
{ ARM::VLD4d32Pseudo,       ARM::VLD4d32,      true,  false, SingleSpc,  4, 2 },
{ ARM::VLD4d32Pseudo_UPD,   ARM::VLD4d32_UPD,  true,  true,  SingleSpc,  4, 2 },
{ ARM::VLD4d8Pseudo,        ARM::VLD4d8,       true,  false, SingleSpc,  4, 8 },
{ ARM::VLD4d8Pseudo_UPD,    ARM::VLD4d8_UPD,   true,  true,  SingleSpc,  4, 8 },

{ ARM::VLD4q16Pseudo_UPD,    ARM::VLD4q16_UPD, true,  true,  EvenDblSpc, 4, 4 },
{ ARM::VLD4q16oddPseudo,     ARM::VLD4q16,     true,  false, OddDblSpc,  4, 4 },
{ ARM::VLD4q16oddPseudo_UPD, ARM::VLD4q16_UPD, true,  true,  OddDblSpc,  4, 4 },
{ ARM::VLD4q32Pseudo_UPD,    ARM::VLD4q32_UPD, true,  true,  EvenDblSpc, 4, 2 },
{ ARM::VLD4q32oddPseudo,     ARM::VLD4q32,     true,  false, OddDblSpc,  4, 2 },
{ ARM::VLD4q32oddPseudo_UPD, ARM::VLD4q32_UPD, true,  true,  OddDblSpc,  4, 2 },
{ ARM::VLD4q8Pseudo_UPD,     ARM::VLD4q8_UPD,  true,  true,  EvenDblSpc, 4, 8 },
{ ARM::VLD4q8oddPseudo,      ARM::VLD4q8,      true,  false, OddDblSpc,  4, 8 },
{ ARM::VLD4q8oddPseudo_UPD,  ARM::VLD4q8_UPD,  true,  true,  OddDblSpc,  4, 8 },

{ ARM::VST1LNq16Pseudo,     ARM::VST1LNd16,    false, false, EvenDblSpc, 1, 4 },
{ ARM::VST1LNq16Pseudo_UPD, ARM::VST1LNd16_UPD,false, true,  EvenDblSpc, 1, 4 },
{ ARM::VST1LNq32Pseudo,     ARM::VST1LNd32,    false, false, EvenDblSpc, 1, 2 },
{ ARM::VST1LNq32Pseudo_UPD, ARM::VST1LNd32_UPD,false, true,  EvenDblSpc, 1, 2 },
{ ARM::VST1LNq8Pseudo,      ARM::VST1LNd8,     false, false, EvenDblSpc, 1, 8 },
{ ARM::VST1LNq8Pseudo_UPD,  ARM::VST1LNd8_UPD, false, true,  EvenDblSpc, 1, 8 },

{ ARM::VST1d64QPseudo,      ARM::VST1d64Q,     false, false, SingleSpc,  4, 1 },
{ ARM::VST1d64QPseudo_UPD,  ARM::VST1d64Q_UPD, false, true,  SingleSpc,  4, 1 },
{ ARM::VST1d64TPseudo,      ARM::VST1d64T,     false, false, SingleSpc,  3, 1 },
{ ARM::VST1d64TPseudo_UPD,  ARM::VST1d64T_UPD, false, true,  SingleSpc,  3, 1 },

{ ARM::VST1q16Pseudo,       ARM::VST1q16,      false, false, SingleSpc,  2, 4 },
{ ARM::VST1q16Pseudo_UPD,   ARM::VST1q16_UPD,  false, true,  SingleSpc,  2, 4 },
{ ARM::VST1q32Pseudo,       ARM::VST1q32,      false, false, SingleSpc,  2, 2 },
{ ARM::VST1q32Pseudo_UPD,   ARM::VST1q32_UPD,  false, true,  SingleSpc,  2, 2 },
{ ARM::VST1q64Pseudo,       ARM::VST1q64,      false, false, SingleSpc,  2, 1 },
{ ARM::VST1q64Pseudo_UPD,   ARM::VST1q64_UPD,  false, true,  SingleSpc,  2, 1 },
{ ARM::VST1q8Pseudo,        ARM::VST1q8,       false, false, SingleSpc,  2, 8 },
{ ARM::VST1q8Pseudo_UPD,    ARM::VST1q8_UPD,   false, true,  SingleSpc,  2, 8 },

{ ARM::VST2LNd16Pseudo,     ARM::VST2LNd16,     false, false, SingleSpc, 2, 4 },
{ ARM::VST2LNd16Pseudo_UPD, ARM::VST2LNd16_UPD, false, true,  SingleSpc, 2, 4 },
{ ARM::VST2LNd32Pseudo,     ARM::VST2LNd32,     false, false, SingleSpc, 2, 2 },
{ ARM::VST2LNd32Pseudo_UPD, ARM::VST2LNd32_UPD, false, true,  SingleSpc, 2, 2 },
{ ARM::VST2LNd8Pseudo,      ARM::VST2LNd8,      false, false, SingleSpc, 2, 8 },
{ ARM::VST2LNd8Pseudo_UPD,  ARM::VST2LNd8_UPD,  false, true,  SingleSpc, 2, 8 },
{ ARM::VST2LNq16Pseudo,     ARM::VST2LNq16,     false, false, EvenDblSpc, 2, 4},
{ ARM::VST2LNq16Pseudo_UPD, ARM::VST2LNq16_UPD, false, true,  EvenDblSpc, 2, 4},
{ ARM::VST2LNq32Pseudo,     ARM::VST2LNq32,     false, false, EvenDblSpc, 2, 2},
{ ARM::VST2LNq32Pseudo_UPD, ARM::VST2LNq32_UPD, false, true,  EvenDblSpc, 2, 2},

{ ARM::VST2d16Pseudo,       ARM::VST2d16,      false, false, SingleSpc,  2, 4 },
{ ARM::VST2d16Pseudo_UPD,   ARM::VST2d16_UPD,  false, true,  SingleSpc,  2, 4 },
{ ARM::VST2d32Pseudo,       ARM::VST2d32,      false, false, SingleSpc,  2, 2 },
{ ARM::VST2d32Pseudo_UPD,   ARM::VST2d32_UPD,  false, true,  SingleSpc,  2, 2 },
{ ARM::VST2d8Pseudo,        ARM::VST2d8,       false, false, SingleSpc,  2, 8 },
{ ARM::VST2d8Pseudo_UPD,    ARM::VST2d8_UPD,   false, true,  SingleSpc,  2, 8 },

{ ARM::VST2q16Pseudo,       ARM::VST2q16,      false, false, SingleSpc,  4, 4 },
{ ARM::VST2q16Pseudo_UPD,   ARM::VST2q16_UPD,  false, true,  SingleSpc,  4, 4 },
{ ARM::VST2q32Pseudo,       ARM::VST2q32,      false, false, SingleSpc,  4, 2 },
{ ARM::VST2q32Pseudo_UPD,   ARM::VST2q32_UPD,  false, true,  SingleSpc,  4, 2 },
{ ARM::VST2q8Pseudo,        ARM::VST2q8,       false, false, SingleSpc,  4, 8 },
{ ARM::VST2q8Pseudo_UPD,    ARM::VST2q8_UPD,   false, true,  SingleSpc,  4, 8 },

{ ARM::VST3LNd16Pseudo,     ARM::VST3LNd16,     false, false, SingleSpc, 3, 4 },
{ ARM::VST3LNd16Pseudo_UPD, ARM::VST3LNd16_UPD, false, true,  SingleSpc, 3, 4 },
{ ARM::VST3LNd32Pseudo,     ARM::VST3LNd32,     false, false, SingleSpc, 3, 2 },
{ ARM::VST3LNd32Pseudo_UPD, ARM::VST3LNd32_UPD, false, true,  SingleSpc, 3, 2 },
{ ARM::VST3LNd8Pseudo,      ARM::VST3LNd8,      false, false, SingleSpc, 3, 8 },
{ ARM::VST3LNd8Pseudo_UPD,  ARM::VST3LNd8_UPD,  false, true,  SingleSpc, 3, 8 },
{ ARM::VST3LNq16Pseudo,     ARM::VST3LNq16,     false, false, EvenDblSpc, 3, 4},
{ ARM::VST3LNq16Pseudo_UPD, ARM::VST3LNq16_UPD, false, true,  EvenDblSpc, 3, 4},
{ ARM::VST3LNq32Pseudo,     ARM::VST3LNq32,     false, false, EvenDblSpc, 3, 2},
{ ARM::VST3LNq32Pseudo_UPD, ARM::VST3LNq32_UPD, false, true,  EvenDblSpc, 3, 2},

{ ARM::VST3d16Pseudo,       ARM::VST3d16,      false, false, SingleSpc,  3, 4 },
{ ARM::VST3d16Pseudo_UPD,   ARM::VST3d16_UPD,  false, true,  SingleSpc,  3, 4 },
{ ARM::VST3d32Pseudo,       ARM::VST3d32,      false, false, SingleSpc,  3, 2 },
{ ARM::VST3d32Pseudo_UPD,   ARM::VST3d32_UPD,  false, true,  SingleSpc,  3, 2 },
{ ARM::VST3d8Pseudo,        ARM::VST3d8,       false, false, SingleSpc,  3, 8 },
{ ARM::VST3d8Pseudo_UPD,    ARM::VST3d8_UPD,   false, true,  SingleSpc,  3, 8 },

{ ARM::VST3q16Pseudo_UPD,    ARM::VST3q16_UPD, false, true,  EvenDblSpc, 3, 4 },
{ ARM::VST3q16oddPseudo,     ARM::VST3q16,     false, false, OddDblSpc,  3, 4 },
{ ARM::VST3q16oddPseudo_UPD, ARM::VST3q16_UPD, false, true,  OddDblSpc,  3, 4 },
{ ARM::VST3q32Pseudo_UPD,    ARM::VST3q32_UPD, false, true,  EvenDblSpc, 3, 2 },
{ ARM::VST3q32oddPseudo,     ARM::VST3q32,     false, false, OddDblSpc,  3, 2 },
{ ARM::VST3q32oddPseudo_UPD, ARM::VST3q32_UPD, false, true,  OddDblSpc,  3, 2 },
{ ARM::VST3q8Pseudo_UPD,     ARM::VST3q8_UPD,  false, true,  EvenDblSpc, 3, 8 },
{ ARM::VST3q8oddPseudo,      ARM::VST3q8,      false, false, OddDblSpc,  3, 8 },
{ ARM::VST3q8oddPseudo_UPD,  ARM::VST3q8_UPD,  false, true,  OddDblSpc,  3, 8 },

{ ARM::VST4LNd16Pseudo,     ARM::VST4LNd16,     false, false, SingleSpc, 4, 4 },
{ ARM::VST4LNd16Pseudo_UPD, ARM::VST4LNd16_UPD, false, true,  SingleSpc, 4, 4 },
{ ARM::VST4LNd32Pseudo,     ARM::VST4LNd32,     false, false, SingleSpc, 4, 2 },
{ ARM::VST4LNd32Pseudo_UPD, ARM::VST4LNd32_UPD, false, true,  SingleSpc, 4, 2 },
{ ARM::VST4LNd8Pseudo,      ARM::VST4LNd8,      false, false, SingleSpc, 4, 8 },
{ ARM::VST4LNd8Pseudo_UPD,  ARM::VST4LNd8_UPD,  false, true,  SingleSpc, 4, 8 },
{ ARM::VST4LNq16Pseudo,     ARM::VST4LNq16,     false, false, EvenDblSpc, 4, 4},
{ ARM::VST4LNq16Pseudo_UPD, ARM::VST4LNq16_UPD, false, true,  EvenDblSpc, 4, 4},
{ ARM::VST4LNq32Pseudo,     ARM::VST4LNq32,     false, false, EvenDblSpc, 4, 2},
{ ARM::VST4LNq32Pseudo_UPD, ARM::VST4LNq32_UPD, false, true,  EvenDblSpc, 4, 2},

{ ARM::VST4d16Pseudo,       ARM::VST4d16,      false, false, SingleSpc,  4, 4 },
{ ARM::VST4d16Pseudo_UPD,   ARM::VST4d16_UPD,  false, true,  SingleSpc,  4, 4 },
{ ARM::VST4d32Pseudo,       ARM::VST4d32,      false, false, SingleSpc,  4, 2 },
{ ARM::VST4d32Pseudo_UPD,   ARM::VST4d32_UPD,  false, true,  SingleSpc,  4, 2 },
{ ARM::VST4d8Pseudo,        ARM::VST4d8,       false, false, SingleSpc,  4, 8 },
{ ARM::VST4d8Pseudo_UPD,    ARM::VST4d8_UPD,   false, true,  SingleSpc,  4, 8 },

{ ARM::VST4q16Pseudo_UPD,    ARM::VST4q16_UPD, false, true,  EvenDblSpc, 4, 4 },
{ ARM::VST4q16oddPseudo,     ARM::VST4q16,     false, false, OddDblSpc,  4, 4 },
{ ARM::VST4q16oddPseudo_UPD, ARM::VST4q16_UPD, false, true,  OddDblSpc,  4, 4 },
{ ARM::VST4q32Pseudo_UPD,    ARM::VST4q32_UPD, false, true,  EvenDblSpc, 4, 2 },
{ ARM::VST4q32oddPseudo,     ARM::VST4q32,     false, false, OddDblSpc,  4, 2 },
{ ARM::VST4q32oddPseudo_UPD, ARM::VST4q32_UPD, false, true,  OddDblSpc,  4, 2 },
{ ARM::VST4q8Pseudo_UPD,     ARM::VST4q8_UPD,  false, true,  EvenDblSpc, 4, 8 },
{ ARM::VST4q8oddPseudo,      ARM::VST4q8,      false, false, OddDblSpc,  4, 8 },
{ ARM::VST4q8oddPseudo_UPD,  ARM::VST4q8_UPD,  false, true,  OddDblSpc,  4, 8 }
};

/// LookupNEONLdSt - Search the NEONLdStTable for information about a NEON
/// load or store pseudo instruction.
static const NEONLdStTableEntry *LookupNEONLdSt(unsigned Opcode) {
  unsigned NumEntries = array_lengthof(NEONLdStTable);

#ifndef NDEBUG
  // Make sure the table is sorted.
  static bool TableChecked = false;
  if (!TableChecked) {
    for (unsigned i = 0; i != NumEntries-1; ++i)
      assert(NEONLdStTable[i] < NEONLdStTable[i+1] &&
             "NEONLdStTable is not sorted!");
    TableChecked = true;
  }
#endif

  const NEONLdStTableEntry *I =
    std::lower_bound(NEONLdStTable, NEONLdStTable + NumEntries, Opcode);
  if (I != NEONLdStTable + NumEntries && I->PseudoOpc == Opcode)
    return I;
  return NULL;
}

/// GetDSubRegs - Get 4 D subregisters of a Q, QQ, or QQQQ register,
/// corresponding to the specified register spacing.  Not all of the results
/// are necessarily valid, e.g., a Q register only has 2 D subregisters.
static void GetDSubRegs(unsigned Reg, NEONRegSpacing RegSpc,
                        const TargetRegisterInfo *TRI, unsigned &D0,
                        unsigned &D1, unsigned &D2, unsigned &D3) {
  if (RegSpc == SingleSpc) {
    D0 = TRI->getSubReg(Reg, ARM::dsub_0);
    D1 = TRI->getSubReg(Reg, ARM::dsub_1);
    D2 = TRI->getSubReg(Reg, ARM::dsub_2);
    D3 = TRI->getSubReg(Reg, ARM::dsub_3);
  } else if (RegSpc == EvenDblSpc) {
    D0 = TRI->getSubReg(Reg, ARM::dsub_0);
    D1 = TRI->getSubReg(Reg, ARM::dsub_2);
    D2 = TRI->getSubReg(Reg, ARM::dsub_4);
    D3 = TRI->getSubReg(Reg, ARM::dsub_6);
  } else {
    assert(RegSpc == OddDblSpc && "unknown register spacing");
    D0 = TRI->getSubReg(Reg, ARM::dsub_1);
    D1 = TRI->getSubReg(Reg, ARM::dsub_3);
    D2 = TRI->getSubReg(Reg, ARM::dsub_5);
    D3 = TRI->getSubReg(Reg, ARM::dsub_7);
  }
}

/// ExpandVLD - Translate VLD pseudo instructions with Q, QQ or QQQQ register
/// operands to real VLD instructions with D register operands.
void ARMExpandPseudo::ExpandVLD(MachineBasicBlock::iterator &MBBI) {
  MachineInstr &MI = *MBBI;
  MachineBasicBlock &MBB = *MI.getParent();

  const NEONLdStTableEntry *TableEntry = LookupNEONLdSt(MI.getOpcode());
  assert(TableEntry && TableEntry->IsLoad && "NEONLdStTable lookup failed");
  NEONRegSpacing RegSpc = TableEntry->RegSpacing;
  unsigned NumRegs = TableEntry->NumRegs;

  MachineInstrBuilder MIB = BuildMI(MBB, MBBI, MI.getDebugLoc(),
                                    TII->get(TableEntry->RealOpc));
  unsigned OpIdx = 0;

  bool DstIsDead = MI.getOperand(OpIdx).isDead();
  unsigned DstReg = MI.getOperand(OpIdx++).getReg();
  unsigned D0, D1, D2, D3;
  GetDSubRegs(DstReg, RegSpc, TRI, D0, D1, D2, D3);
  MIB.addReg(D0, RegState::Define | getDeadRegState(DstIsDead))
    .addReg(D1, RegState::Define | getDeadRegState(DstIsDead));
  if (NumRegs > 2)
    MIB.addReg(D2, RegState::Define | getDeadRegState(DstIsDead));
  if (NumRegs > 3)
    MIB.addReg(D3, RegState::Define | getDeadRegState(DstIsDead));

  if (TableEntry->HasWriteBack)
    MIB.addOperand(MI.getOperand(OpIdx++));

  // Copy the addrmode6 operands.
  MIB.addOperand(MI.getOperand(OpIdx++));
  MIB.addOperand(MI.getOperand(OpIdx++));
  // Copy the am6offset operand.
  if (TableEntry->HasWriteBack)
    MIB.addOperand(MI.getOperand(OpIdx++));

  // For an instruction writing double-spaced subregs, the pseudo instruction
  // has an extra operand that is a use of the super-register.  Record the
  // operand index and skip over it.
  unsigned SrcOpIdx = 0;
  if (RegSpc == EvenDblSpc || RegSpc == OddDblSpc)
    SrcOpIdx = OpIdx++;

  // Copy the predicate operands.
  MIB.addOperand(MI.getOperand(OpIdx++));
  MIB.addOperand(MI.getOperand(OpIdx++));

  // Copy the super-register source operand used for double-spaced subregs over
  // to the new instruction as an implicit operand.
  if (SrcOpIdx != 0) {
    MachineOperand MO = MI.getOperand(SrcOpIdx);
    MO.setImplicit(true);
    MIB.addOperand(MO);
  }
  // Add an implicit def for the super-register.
  MIB.addReg(DstReg, RegState::ImplicitDefine | getDeadRegState(DstIsDead));
  TransferImpOps(MI, MIB, MIB);

  // Transfer memoperands.
  MIB->setMemRefs(MI.memoperands_begin(), MI.memoperands_end());

  MI.eraseFromParent();
}

/// ExpandVST - Translate VST pseudo instructions with Q, QQ or QQQQ register
/// operands to real VST instructions with D register operands.
void ARMExpandPseudo::ExpandVST(MachineBasicBlock::iterator &MBBI) {
  MachineInstr &MI = *MBBI;
  MachineBasicBlock &MBB = *MI.getParent();

  const NEONLdStTableEntry *TableEntry = LookupNEONLdSt(MI.getOpcode());
  assert(TableEntry && !TableEntry->IsLoad && "NEONLdStTable lookup failed");
  NEONRegSpacing RegSpc = TableEntry->RegSpacing;
  unsigned NumRegs = TableEntry->NumRegs;

  MachineInstrBuilder MIB = BuildMI(MBB, MBBI, MI.getDebugLoc(),
                                    TII->get(TableEntry->RealOpc));
  unsigned OpIdx = 0;
  if (TableEntry->HasWriteBack)
    MIB.addOperand(MI.getOperand(OpIdx++));

  // Copy the addrmode6 operands.
  MIB.addOperand(MI.getOperand(OpIdx++));
  MIB.addOperand(MI.getOperand(OpIdx++));
  // Copy the am6offset operand.
  if (TableEntry->HasWriteBack)
    MIB.addOperand(MI.getOperand(OpIdx++));

  bool SrcIsKill = MI.getOperand(OpIdx).isKill();
  unsigned SrcReg = MI.getOperand(OpIdx++).getReg();
  unsigned D0, D1, D2, D3;
  GetDSubRegs(SrcReg, RegSpc, TRI, D0, D1, D2, D3);
  MIB.addReg(D0).addReg(D1);
  if (NumRegs > 2)
    MIB.addReg(D2);
  if (NumRegs > 3)
    MIB.addReg(D3);

  // Copy the predicate operands.
  MIB.addOperand(MI.getOperand(OpIdx++));
  MIB.addOperand(MI.getOperand(OpIdx++));

  if (SrcIsKill) // Add an implicit kill for the super-reg.
    MIB->addRegisterKilled(SrcReg, TRI, true);
  TransferImpOps(MI, MIB, MIB);

  // Transfer memoperands.
  MIB->setMemRefs(MI.memoperands_begin(), MI.memoperands_end());

  MI.eraseFromParent();
}

/// ExpandLaneOp - Translate VLD*LN and VST*LN instructions with Q, QQ or QQQQ
/// register operands to real instructions with D register operands.
void ARMExpandPseudo::ExpandLaneOp(MachineBasicBlock::iterator &MBBI) {
  MachineInstr &MI = *MBBI;
  MachineBasicBlock &MBB = *MI.getParent();

  const NEONLdStTableEntry *TableEntry = LookupNEONLdSt(MI.getOpcode());
  assert(TableEntry && "NEONLdStTable lookup failed");
  NEONRegSpacing RegSpc = TableEntry->RegSpacing;
  unsigned NumRegs = TableEntry->NumRegs;
  unsigned RegElts = TableEntry->RegElts;

  MachineInstrBuilder MIB = BuildMI(MBB, MBBI, MI.getDebugLoc(),
                                    TII->get(TableEntry->RealOpc));
  unsigned OpIdx = 0;
  // The lane operand is always the 3rd from last operand, before the 2
  // predicate operands.
  unsigned Lane = MI.getOperand(MI.getDesc().getNumOperands() - 3).getImm();

  // Adjust the lane and spacing as needed for Q registers.
  assert(RegSpc != OddDblSpc && "unexpected register spacing for VLD/VST-lane");
  if (RegSpc == EvenDblSpc && Lane >= RegElts) {
    RegSpc = OddDblSpc;
    Lane -= RegElts;
  }
  assert(Lane < RegElts && "out of range lane for VLD/VST-lane");

  unsigned D0 = 0, D1 = 0, D2 = 0, D3 = 0;
  unsigned DstReg = 0;
  bool DstIsDead = false;
  if (TableEntry->IsLoad) {
    DstIsDead = MI.getOperand(OpIdx).isDead();
    DstReg = MI.getOperand(OpIdx++).getReg();
    GetDSubRegs(DstReg, RegSpc, TRI, D0, D1, D2, D3);
    MIB.addReg(D0, RegState::Define | getDeadRegState(DstIsDead));
    if (NumRegs > 1)
      MIB.addReg(D1, RegState::Define | getDeadRegState(DstIsDead));
    if (NumRegs > 2)
      MIB.addReg(D2, RegState::Define | getDeadRegState(DstIsDead));
    if (NumRegs > 3)
      MIB.addReg(D3, RegState::Define | getDeadRegState(DstIsDead));
  }

  if (TableEntry->HasWriteBack)
    MIB.addOperand(MI.getOperand(OpIdx++));

  // Copy the addrmode6 operands.
  MIB.addOperand(MI.getOperand(OpIdx++));
  MIB.addOperand(MI.getOperand(OpIdx++));
  // Copy the am6offset operand.
  if (TableEntry->HasWriteBack)
    MIB.addOperand(MI.getOperand(OpIdx++));

  // Grab the super-register source.
  MachineOperand MO = MI.getOperand(OpIdx++);
  if (!TableEntry->IsLoad)
    GetDSubRegs(MO.getReg(), RegSpc, TRI, D0, D1, D2, D3);

  // Add the subregs as sources of the new instruction.
  unsigned SrcFlags = (getUndefRegState(MO.isUndef()) |
                       getKillRegState(MO.isKill()));
  MIB.addReg(D0, SrcFlags);
  if (NumRegs > 1)
    MIB.addReg(D1, SrcFlags);
  if (NumRegs > 2)
    MIB.addReg(D2, SrcFlags);
  if (NumRegs > 3)
    MIB.addReg(D3, SrcFlags);

  // Add the lane number operand.
  MIB.addImm(Lane);
  OpIdx += 1;

  // Copy the predicate operands.
  MIB.addOperand(MI.getOperand(OpIdx++));
  MIB.addOperand(MI.getOperand(OpIdx++));

  // Copy the super-register source to be an implicit source.
  MO.setImplicit(true);
  MIB.addOperand(MO);
  if (TableEntry->IsLoad)
    // Add an implicit def for the super-register.
    MIB.addReg(DstReg, RegState::ImplicitDefine | getDeadRegState(DstIsDead));
  TransferImpOps(MI, MIB, MIB);
  MI.eraseFromParent();
}

/// ExpandVTBL - Translate VTBL and VTBX pseudo instructions with Q or QQ
/// register operands to real instructions with D register operands.
void ARMExpandPseudo::ExpandVTBL(MachineBasicBlock::iterator &MBBI,
                                 unsigned Opc, bool IsExt, unsigned NumRegs) {
  MachineInstr &MI = *MBBI;
  MachineBasicBlock &MBB = *MI.getParent();

  MachineInstrBuilder MIB = BuildMI(MBB, MBBI, MI.getDebugLoc(), TII->get(Opc));
  unsigned OpIdx = 0;

  // Transfer the destination register operand.
  MIB.addOperand(MI.getOperand(OpIdx++));
  if (IsExt)
    MIB.addOperand(MI.getOperand(OpIdx++));

  bool SrcIsKill = MI.getOperand(OpIdx).isKill();
  unsigned SrcReg = MI.getOperand(OpIdx++).getReg();
  unsigned D0, D1, D2, D3;
  GetDSubRegs(SrcReg, SingleSpc, TRI, D0, D1, D2, D3);
  MIB.addReg(D0).addReg(D1);
  if (NumRegs > 2)
    MIB.addReg(D2);
  if (NumRegs > 3)
    MIB.addReg(D3);

  // Copy the other source register operand.
  MIB.addOperand(MI.getOperand(OpIdx++));

  // Copy the predicate operands.
  MIB.addOperand(MI.getOperand(OpIdx++));
  MIB.addOperand(MI.getOperand(OpIdx++));

  if (SrcIsKill)  // Add an implicit kill for the super-reg.
    MIB->addRegisterKilled(SrcReg, TRI, true);
  TransferImpOps(MI, MIB, MIB);
  MI.eraseFromParent();
}

void ARMExpandPseudo::ExpandMOV32BitImm(MachineBasicBlock &MBB,
                                        MachineBasicBlock::iterator &MBBI) {
  MachineInstr &MI = *MBBI;
  unsigned Opcode = MI.getOpcode();
  unsigned PredReg = 0;
  ARMCC::CondCodes Pred = llvm::getInstrPredicate(&MI, PredReg);
  unsigned DstReg = MI.getOperand(0).getReg();
  bool DstIsDead = MI.getOperand(0).isDead();
  bool isCC = Opcode == ARM::MOVCCi32imm || Opcode == ARM::t2MOVCCi32imm;
  const MachineOperand &MO = MI.getOperand(isCC ? 2 : 1);
  MachineInstrBuilder LO16, HI16;

  if (!STI->hasV6T2Ops() &&
      (Opcode == ARM::MOVi32imm || Opcode == ARM::MOVCCi32imm)) {
    // Expand into a movi + orr.
    LO16 = BuildMI(MBB, MBBI, MI.getDebugLoc(), TII->get(ARM::MOVi), DstReg);
    HI16 = BuildMI(MBB, MBBI, MI.getDebugLoc(), TII->get(ARM::ORRri))
      .addReg(DstReg, RegState::Define | getDeadRegState(DstIsDead))
      .addReg(DstReg);

    assert (MO.isImm() && "MOVi32imm w/ non-immediate source operand!");
    unsigned ImmVal = (unsigned)MO.getImm();
    unsigned SOImmValV1 = ARM_AM::getSOImmTwoPartFirst(ImmVal);
    unsigned SOImmValV2 = ARM_AM::getSOImmTwoPartSecond(ImmVal);
    LO16 = LO16.addImm(SOImmValV1);
    HI16 = HI16.addImm(SOImmValV2);
    LO16->setMemRefs(MI.memoperands_begin(), MI.memoperands_end());
    HI16->setMemRefs(MI.memoperands_begin(), MI.memoperands_end());
    LO16.addImm(Pred).addReg(PredReg).addReg(0);
    HI16.addImm(Pred).addReg(PredReg).addReg(0);
    TransferImpOps(MI, LO16, HI16);
    MI.eraseFromParent();
    return;
  }

  unsigned LO16Opc = 0;
  unsigned HI16Opc = 0;
  if (Opcode == ARM::t2MOVi32imm || Opcode == ARM::t2MOVCCi32imm) {
    LO16Opc = ARM::t2MOVi16;
    HI16Opc = ARM::t2MOVTi16;
  } else {
    LO16Opc = ARM::MOVi16;
    HI16Opc = ARM::MOVTi16;
  }

  LO16 = BuildMI(MBB, MBBI, MI.getDebugLoc(), TII->get(LO16Opc), DstReg);
  HI16 = BuildMI(MBB, MBBI, MI.getDebugLoc(), TII->get(HI16Opc))
    .addReg(DstReg, RegState::Define | getDeadRegState(DstIsDead))
    .addReg(DstReg);

  if (MO.isImm()) {
    unsigned Imm = MO.getImm();
    unsigned Lo16 = Imm & 0xffff;
    unsigned Hi16 = (Imm >> 16) & 0xffff;
    LO16 = LO16.addImm(Lo16);
    HI16 = HI16.addImm(Hi16);
  } else {
    const GlobalValue *GV = MO.getGlobal();
    unsigned TF = MO.getTargetFlags();
    LO16 = LO16.addGlobalAddress(GV, MO.getOffset(), TF | ARMII::MO_LO16);
    HI16 = HI16.addGlobalAddress(GV, MO.getOffset(), TF | ARMII::MO_HI16);
  }

  LO16->setMemRefs(MI.memoperands_begin(), MI.memoperands_end());
  HI16->setMemRefs(MI.memoperands_begin(), MI.memoperands_end());
  LO16.addImm(Pred).addReg(PredReg);
  HI16.addImm(Pred).addReg(PredReg);

  TransferImpOps(MI, LO16, HI16);
  MI.eraseFromParent();
}

bool ARMExpandPseudo::ExpandMI(MachineBasicBlock &MBB,
                               MachineBasicBlock::iterator MBBI) {
  MachineInstr &MI = *MBBI;
  unsigned Opcode = MI.getOpcode();
  switch (Opcode) {
    default:
      return false;
    case ARM::VMOVScc:
    case ARM::VMOVDcc: {
      unsigned newOpc = Opcode == ARM::VMOVScc ? ARM::VMOVS : ARM::VMOVD;
      BuildMI(MBB, MBBI, MI.getDebugLoc(), TII->get(newOpc),
              MI.getOperand(1).getReg())
        .addReg(MI.getOperand(2).getReg(),
                getKillRegState(MI.getOperand(2).isKill()))
        .addImm(MI.getOperand(3).getImm()) // 'pred'
        .addReg(MI.getOperand(4).getReg());

      MI.eraseFromParent();
      return true;
    }
    case ARM::t2MOVCCr:
    case ARM::MOVCCr: {
      unsigned Opc = AFI->isThumbFunction() ? ARM::t2MOVr : ARM::MOVr;
      BuildMI(MBB, MBBI, MI.getDebugLoc(), TII->get(Opc),
              MI.getOperand(1).getReg())
        .addReg(MI.getOperand(2).getReg(),
                getKillRegState(MI.getOperand(2).isKill()))
        .addImm(MI.getOperand(3).getImm()) // 'pred'
        .addReg(MI.getOperand(4).getReg())
        .addReg(0); // 's' bit

      MI.eraseFromParent();
      return true;
    }
    case ARM::MOVCCsi: {
      BuildMI(MBB, MBBI, MI.getDebugLoc(), TII->get(ARM::MOVsi),
              (MI.getOperand(1).getReg()))
        .addReg(MI.getOperand(2).getReg(),
                getKillRegState(MI.getOperand(2).isKill()))
        .addImm(MI.getOperand(3).getImm())
        .addImm(MI.getOperand(4).getImm()) // 'pred'
        .addReg(MI.getOperand(5).getReg())
        .addReg(0); // 's' bit

      MI.eraseFromParent();
      return true;
    }

    case ARM::MOVCCsr: {
      BuildMI(MBB, MBBI, MI.getDebugLoc(), TII->get(ARM::MOVsr),
              (MI.getOperand(1).getReg()))
        .addReg(MI.getOperand(2).getReg(),
                getKillRegState(MI.getOperand(2).isKill()))
        .addReg(MI.getOperand(3).getReg(),
                getKillRegState(MI.getOperand(3).isKill()))
        .addImm(MI.getOperand(4).getImm())
        .addImm(MI.getOperand(5).getImm()) // 'pred'
        .addReg(MI.getOperand(6).getReg())
        .addReg(0); // 's' bit

      MI.eraseFromParent();
      return true;
    }
    case ARM::MOVCCi16: {
      BuildMI(MBB, MBBI, MI.getDebugLoc(), TII->get(ARM::MOVi16),
              MI.getOperand(1).getReg())
        .addImm(MI.getOperand(2).getImm())
        .addImm(MI.getOperand(3).getImm()) // 'pred'
        .addReg(MI.getOperand(4).getReg());

      MI.eraseFromParent();
      return true;
    }
    case ARM::t2MOVCCi:
    case ARM::MOVCCi: {
      unsigned Opc = AFI->isThumbFunction() ? ARM::t2MOVi : ARM::MOVi;
      BuildMI(MBB, MBBI, MI.getDebugLoc(), TII->get(Opc),
              MI.getOperand(1).getReg())
        .addImm(MI.getOperand(2).getImm())
        .addImm(MI.getOperand(3).getImm()) // 'pred'
        .addReg(MI.getOperand(4).getReg())
        .addReg(0); // 's' bit

      MI.eraseFromParent();
      return true;
    }
    case ARM::MVNCCi: {
      BuildMI(MBB, MBBI, MI.getDebugLoc(), TII->get(ARM::MVNi),
              MI.getOperand(1).getReg())
        .addImm(MI.getOperand(2).getImm())
        .addImm(MI.getOperand(3).getImm()) // 'pred'
        .addReg(MI.getOperand(4).getReg())
        .addReg(0); // 's' bit

      MI.eraseFromParent();
      return true;
    }
    case ARM::Int_eh_sjlj_dispatchsetup: {
      MachineFunction &MF = *MI.getParent()->getParent();
      const ARMBaseInstrInfo *AII =
        static_cast<const ARMBaseInstrInfo*>(TII);
      const ARMBaseRegisterInfo &RI = AII->getRegisterInfo();
      // For functions using a base pointer, we rematerialize it (via the frame
      // pointer) here since eh.sjlj.setjmp and eh.sjlj.longjmp don't do it
      // for us. Otherwise, expand to nothing.
      if (RI.hasBasePointer(MF)) {
        int32_t NumBytes = AFI->getFramePtrSpillOffset();
        unsigned FramePtr = RI.getFrameRegister(MF);
        assert(MF.getTarget().getFrameLowering()->hasFP(MF) &&
               "base pointer without frame pointer?");

        if (AFI->isThumb2Function()) {
          llvm::emitT2RegPlusImmediate(MBB, MBBI, MI.getDebugLoc(), ARM::R6,
                                       FramePtr, -NumBytes, ARMCC::AL, 0, *TII);
        } else if (AFI->isThumbFunction()) {
          llvm::emitThumbRegPlusImmediate(MBB, MBBI, MI.getDebugLoc(), ARM::R6,
                                          FramePtr, -NumBytes, *TII, RI);
        } else {
          llvm::emitARMRegPlusImmediate(MBB, MBBI, MI.getDebugLoc(), ARM::R6,
                                        FramePtr, -NumBytes, ARMCC::AL, 0,
                                        *TII);
        }
        // If there's dynamic realignment, adjust for it.
        if (RI.needsStackRealignment(MF)) {
          MachineFrameInfo  *MFI = MF.getFrameInfo();
          unsigned MaxAlign = MFI->getMaxAlignment();
          assert (!AFI->isThumb1OnlyFunction());
          // Emit bic r6, r6, MaxAlign
          unsigned bicOpc = AFI->isThumbFunction() ?
            ARM::t2BICri : ARM::BICri;
          AddDefaultCC(AddDefaultPred(BuildMI(MBB, MBBI, MI.getDebugLoc(),
                                              TII->get(bicOpc), ARM::R6)
                                      .addReg(ARM::R6, RegState::Kill)
                                      .addImm(MaxAlign-1)));
        }

      }
      MI.eraseFromParent();
      return true;
    }

    case ARM::MOVsrl_flag:
    case ARM::MOVsra_flag: {
      // These are just fancy MOVs insructions.
      AddDefaultPred(BuildMI(MBB, MBBI, MI.getDebugLoc(), TII->get(ARM::MOVsi),
                             MI.getOperand(0).getReg())
                     .addOperand(MI.getOperand(1))
                     .addImm(ARM_AM::getSORegOpc((Opcode == ARM::MOVsrl_flag ?
                                                  ARM_AM::lsr : ARM_AM::asr),
                                                 1)))
        .addReg(ARM::CPSR, RegState::Define);
      MI.eraseFromParent();
      return true;
    }
    case ARM::RRX: {
      // This encodes as "MOVs Rd, Rm, rrx
      MachineInstrBuilder MIB =
        AddDefaultPred(BuildMI(MBB, MBBI, MI.getDebugLoc(), TII->get(ARM::MOVsi),
                               MI.getOperand(0).getReg())
                       .addOperand(MI.getOperand(1))
                       .addImm(ARM_AM::getSORegOpc(ARM_AM::rrx, 0)))
        .addReg(0);
      TransferImpOps(MI, MIB, MIB);
      MI.eraseFromParent();
      return true;
    }
    case ARM::tTPsoft:
    case ARM::TPsoft: {
      MachineInstrBuilder MIB =
        BuildMI(MBB, MBBI, MI.getDebugLoc(),
                TII->get(Opcode == ARM::tTPsoft ? ARM::tBL : ARM::BL))
        .addExternalSymbol("__aeabi_read_tp", 0);

      MIB->setMemRefs(MI.memoperands_begin(), MI.memoperands_end());
      TransferImpOps(MI, MIB, MIB);
      MI.eraseFromParent();
      return true;
    }
    case ARM::tLDRpci_pic:
    case ARM::t2LDRpci_pic: {
      unsigned NewLdOpc = (Opcode == ARM::tLDRpci_pic)
        ? ARM::tLDRpci : ARM::t2LDRpci;
      unsigned DstReg = MI.getOperand(0).getReg();
      bool DstIsDead = MI.getOperand(0).isDead();
      MachineInstrBuilder MIB1 =
        AddDefaultPred(BuildMI(MBB, MBBI, MI.getDebugLoc(),
                               TII->get(NewLdOpc), DstReg)
                       .addOperand(MI.getOperand(1)));
      MIB1->setMemRefs(MI.memoperands_begin(), MI.memoperands_end());
      MachineInstrBuilder MIB2 = BuildMI(MBB, MBBI, MI.getDebugLoc(),
                                         TII->get(ARM::tPICADD))
        .addReg(DstReg, RegState::Define | getDeadRegState(DstIsDead))
        .addReg(DstReg)
        .addOperand(MI.getOperand(2));
      TransferImpOps(MI, MIB1, MIB2);
      MI.eraseFromParent();
      return true;
    }

    case ARM::MOV_ga_dyn:
    case ARM::MOV_ga_pcrel:
    case ARM::MOV_ga_pcrel_ldr:
    case ARM::t2MOV_ga_dyn:
    case ARM::t2MOV_ga_pcrel: {
      // Expand into movw + movw. Also "add pc" / ldr [pc] in PIC mode.
      unsigned LabelId = AFI->createPICLabelUId();
      unsigned DstReg = MI.getOperand(0).getReg();
      bool DstIsDead = MI.getOperand(0).isDead();
      const MachineOperand &MO1 = MI.getOperand(1);
      const GlobalValue *GV = MO1.getGlobal();
      unsigned TF = MO1.getTargetFlags();
      bool isARM = (Opcode != ARM::t2MOV_ga_pcrel && Opcode!=ARM::t2MOV_ga_dyn);
      bool isPIC = (Opcode != ARM::MOV_ga_dyn && Opcode != ARM::t2MOV_ga_dyn);
      unsigned LO16Opc = isARM ? ARM::MOVi16_ga_pcrel : ARM::t2MOVi16_ga_pcrel;
      unsigned HI16Opc = isARM ? ARM::MOVTi16_ga_pcrel :ARM::t2MOVTi16_ga_pcrel;
      unsigned LO16TF = isPIC
        ? ARMII::MO_LO16_NONLAZY_PIC : ARMII::MO_LO16_NONLAZY;
      unsigned HI16TF = isPIC
        ? ARMII::MO_HI16_NONLAZY_PIC : ARMII::MO_HI16_NONLAZY;
      unsigned PICAddOpc = isARM
        ? (Opcode == ARM::MOV_ga_pcrel_ldr ? ARM::PICLDR : ARM::PICADD)
        : ARM::tPICADD;
      MachineInstrBuilder MIB1 = BuildMI(MBB, MBBI, MI.getDebugLoc(),
                                         TII->get(LO16Opc), DstReg)
        .addGlobalAddress(GV, MO1.getOffset(), TF | LO16TF)
        .addImm(LabelId);
      MachineInstrBuilder MIB2 = BuildMI(MBB, MBBI, MI.getDebugLoc(),
                                         TII->get(HI16Opc), DstReg)
        .addReg(DstReg)
        .addGlobalAddress(GV, MO1.getOffset(), TF | HI16TF)
        .addImm(LabelId);
      if (!isPIC) {
        TransferImpOps(MI, MIB1, MIB2);
        MI.eraseFromParent();
        return true;
      }

      MachineInstrBuilder MIB3 = BuildMI(MBB, MBBI, MI.getDebugLoc(),
                                         TII->get(PICAddOpc))
        .addReg(DstReg, RegState::Define | getDeadRegState(DstIsDead))
        .addReg(DstReg).addImm(LabelId);
      if (isARM) {
        AddDefaultPred(MIB3);
        if (Opcode == ARM::MOV_ga_pcrel_ldr)
          MIB2->setMemRefs(MI.memoperands_begin(), MI.memoperands_end());
      }
      TransferImpOps(MI, MIB1, MIB3);
      MI.eraseFromParent();
      return true;
    }

    case ARM::MOVi32imm:
    case ARM::MOVCCi32imm:
    case ARM::t2MOVi32imm:
    case ARM::t2MOVCCi32imm:
      ExpandMOV32BitImm(MBB, MBBI);
      return true;

    case ARM::VMOVQQ: {
      unsigned DstReg = MI.getOperand(0).getReg();
      bool DstIsDead = MI.getOperand(0).isDead();
      unsigned EvenDst = TRI->getSubReg(DstReg, ARM::qsub_0);
      unsigned OddDst  = TRI->getSubReg(DstReg, ARM::qsub_1);
      unsigned SrcReg = MI.getOperand(1).getReg();
      bool SrcIsKill = MI.getOperand(1).isKill();
      unsigned EvenSrc = TRI->getSubReg(SrcReg, ARM::qsub_0);
      unsigned OddSrc  = TRI->getSubReg(SrcReg, ARM::qsub_1);
      MachineInstrBuilder Even =
        AddDefaultPred(BuildMI(MBB, MBBI, MI.getDebugLoc(),
                               TII->get(ARM::VORRq))
                       .addReg(EvenDst,
                               RegState::Define | getDeadRegState(DstIsDead))
                       .addReg(EvenSrc, getKillRegState(SrcIsKill))
                       .addReg(EvenSrc, getKillRegState(SrcIsKill)));
      MachineInstrBuilder Odd =
        AddDefaultPred(BuildMI(MBB, MBBI, MI.getDebugLoc(),
                               TII->get(ARM::VORRq))
                       .addReg(OddDst,
                               RegState::Define | getDeadRegState(DstIsDead))
                       .addReg(OddSrc, getKillRegState(SrcIsKill))
                       .addReg(OddSrc, getKillRegState(SrcIsKill)));
      TransferImpOps(MI, Even, Odd);
      MI.eraseFromParent();
      return true;
    }

    case ARM::VLDMQIA: {
      unsigned NewOpc = ARM::VLDMDIA;
      MachineInstrBuilder MIB =
        BuildMI(MBB, MBBI, MI.getDebugLoc(), TII->get(NewOpc));
      unsigned OpIdx = 0;

      // Grab the Q register destination.
      bool DstIsDead = MI.getOperand(OpIdx).isDead();
      unsigned DstReg = MI.getOperand(OpIdx++).getReg();

      // Copy the source register.
      MIB.addOperand(MI.getOperand(OpIdx++));

      // Copy the predicate operands.
      MIB.addOperand(MI.getOperand(OpIdx++));
      MIB.addOperand(MI.getOperand(OpIdx++));

      // Add the destination operands (D subregs).
      unsigned D0 = TRI->getSubReg(DstReg, ARM::dsub_0);
      unsigned D1 = TRI->getSubReg(DstReg, ARM::dsub_1);
      MIB.addReg(D0, RegState::Define | getDeadRegState(DstIsDead))
        .addReg(D1, RegState::Define | getDeadRegState(DstIsDead));

      // Add an implicit def for the super-register.
      MIB.addReg(DstReg, RegState::ImplicitDefine | getDeadRegState(DstIsDead));
      TransferImpOps(MI, MIB, MIB);
      MI.eraseFromParent();
      return true;
    }

    case ARM::VSTMQIA: {
      unsigned NewOpc = ARM::VSTMDIA;
      MachineInstrBuilder MIB =
        BuildMI(MBB, MBBI, MI.getDebugLoc(), TII->get(NewOpc));
      unsigned OpIdx = 0;

      // Grab the Q register source.
      bool SrcIsKill = MI.getOperand(OpIdx).isKill();
      unsigned SrcReg = MI.getOperand(OpIdx++).getReg();

      // Copy the destination register.
      MIB.addOperand(MI.getOperand(OpIdx++));

      // Copy the predicate operands.
      MIB.addOperand(MI.getOperand(OpIdx++));
      MIB.addOperand(MI.getOperand(OpIdx++));

      // Add the source operands (D subregs).
      unsigned D0 = TRI->getSubReg(SrcReg, ARM::dsub_0);
      unsigned D1 = TRI->getSubReg(SrcReg, ARM::dsub_1);
      MIB.addReg(D0).addReg(D1);

      if (SrcIsKill)      // Add an implicit kill for the Q register.
        MIB->addRegisterKilled(SrcReg, TRI, true);

      TransferImpOps(MI, MIB, MIB);
      MI.eraseFromParent();
      return true;
    }
    case ARM::VDUPfqf:
    case ARM::VDUPfdf:{
      unsigned NewOpc = Opcode == ARM::VDUPfqf ? ARM::VDUPLN32q :
        ARM::VDUPLN32d;
      MachineInstrBuilder MIB =
        BuildMI(MBB, MBBI, MI.getDebugLoc(), TII->get(NewOpc));
      unsigned OpIdx = 0;
      unsigned SrcReg = MI.getOperand(1).getReg();
      unsigned Lane = getARMRegisterNumbering(SrcReg) & 1;
      unsigned DReg = TRI->getMatchingSuperReg(SrcReg,
                            Lane & 1 ? ARM::ssub_1 : ARM::ssub_0,
                            &ARM::DPR_VFP2RegClass);
      // The lane is [0,1] for the containing DReg superregister.
      // Copy the dst/src register operands.
      MIB.addOperand(MI.getOperand(OpIdx++));
      MIB.addReg(DReg);
      ++OpIdx;
      // Add the lane select operand.
      MIB.addImm(Lane);
      // Add the predicate operands.
      MIB.addOperand(MI.getOperand(OpIdx++));
      MIB.addOperand(MI.getOperand(OpIdx++));

      TransferImpOps(MI, MIB, MIB);
      MI.eraseFromParent();
      return true;
    }

    case ARM::VLD1q8Pseudo:
    case ARM::VLD1q16Pseudo:
    case ARM::VLD1q32Pseudo:
    case ARM::VLD1q64Pseudo:
    case ARM::VLD1q8Pseudo_UPD:
    case ARM::VLD1q16Pseudo_UPD:
    case ARM::VLD1q32Pseudo_UPD:
    case ARM::VLD1q64Pseudo_UPD:
    case ARM::VLD2d8Pseudo:
    case ARM::VLD2d16Pseudo:
    case ARM::VLD2d32Pseudo:
    case ARM::VLD2q8Pseudo:
    case ARM::VLD2q16Pseudo:
    case ARM::VLD2q32Pseudo:
    case ARM::VLD2d8Pseudo_UPD:
    case ARM::VLD2d16Pseudo_UPD:
    case ARM::VLD2d32Pseudo_UPD:
    case ARM::VLD2q8Pseudo_UPD:
    case ARM::VLD2q16Pseudo_UPD:
    case ARM::VLD2q32Pseudo_UPD:
    case ARM::VLD3d8Pseudo:
    case ARM::VLD3d16Pseudo:
    case ARM::VLD3d32Pseudo:
    case ARM::VLD1d64TPseudo:
    case ARM::VLD3d8Pseudo_UPD:
    case ARM::VLD3d16Pseudo_UPD:
    case ARM::VLD3d32Pseudo_UPD:
    case ARM::VLD1d64TPseudo_UPD:
    case ARM::VLD3q8Pseudo_UPD:
    case ARM::VLD3q16Pseudo_UPD:
    case ARM::VLD3q32Pseudo_UPD:
    case ARM::VLD3q8oddPseudo:
    case ARM::VLD3q16oddPseudo:
    case ARM::VLD3q32oddPseudo:
    case ARM::VLD3q8oddPseudo_UPD:
    case ARM::VLD3q16oddPseudo_UPD:
    case ARM::VLD3q32oddPseudo_UPD:
    case ARM::VLD4d8Pseudo:
    case ARM::VLD4d16Pseudo:
    case ARM::VLD4d32Pseudo:
    case ARM::VLD1d64QPseudo:
    case ARM::VLD4d8Pseudo_UPD:
    case ARM::VLD4d16Pseudo_UPD:
    case ARM::VLD4d32Pseudo_UPD:
    case ARM::VLD1d64QPseudo_UPD:
    case ARM::VLD4q8Pseudo_UPD:
    case ARM::VLD4q16Pseudo_UPD:
    case ARM::VLD4q32Pseudo_UPD:
    case ARM::VLD4q8oddPseudo:
    case ARM::VLD4q16oddPseudo:
    case ARM::VLD4q32oddPseudo:
    case ARM::VLD4q8oddPseudo_UPD:
    case ARM::VLD4q16oddPseudo_UPD:
    case ARM::VLD4q32oddPseudo_UPD:
    case ARM::VLD1DUPq8Pseudo:
    case ARM::VLD1DUPq16Pseudo:
    case ARM::VLD1DUPq32Pseudo:
    case ARM::VLD1DUPq8Pseudo_UPD:
    case ARM::VLD1DUPq16Pseudo_UPD:
    case ARM::VLD1DUPq32Pseudo_UPD:
    case ARM::VLD2DUPd8Pseudo:
    case ARM::VLD2DUPd16Pseudo:
    case ARM::VLD2DUPd32Pseudo:
    case ARM::VLD2DUPd8Pseudo_UPD:
    case ARM::VLD2DUPd16Pseudo_UPD:
    case ARM::VLD2DUPd32Pseudo_UPD:
    case ARM::VLD3DUPd8Pseudo:
    case ARM::VLD3DUPd16Pseudo:
    case ARM::VLD3DUPd32Pseudo:
    case ARM::VLD3DUPd8Pseudo_UPD:
    case ARM::VLD3DUPd16Pseudo_UPD:
    case ARM::VLD3DUPd32Pseudo_UPD:
    case ARM::VLD4DUPd8Pseudo:
    case ARM::VLD4DUPd16Pseudo:
    case ARM::VLD4DUPd32Pseudo:
    case ARM::VLD4DUPd8Pseudo_UPD:
    case ARM::VLD4DUPd16Pseudo_UPD:
    case ARM::VLD4DUPd32Pseudo_UPD:
      ExpandVLD(MBBI);
      return true;

    case ARM::VST1q8Pseudo:
    case ARM::VST1q16Pseudo:
    case ARM::VST1q32Pseudo:
    case ARM::VST1q64Pseudo:
    case ARM::VST1q8Pseudo_UPD:
    case ARM::VST1q16Pseudo_UPD:
    case ARM::VST1q32Pseudo_UPD:
    case ARM::VST1q64Pseudo_UPD:
    case ARM::VST2d8Pseudo:
    case ARM::VST2d16Pseudo:
    case ARM::VST2d32Pseudo:
    case ARM::VST2q8Pseudo:
    case ARM::VST2q16Pseudo:
    case ARM::VST2q32Pseudo:
    case ARM::VST2d8Pseudo_UPD:
    case ARM::VST2d16Pseudo_UPD:
    case ARM::VST2d32Pseudo_UPD:
    case ARM::VST2q8Pseudo_UPD:
    case ARM::VST2q16Pseudo_UPD:
    case ARM::VST2q32Pseudo_UPD:
    case ARM::VST3d8Pseudo:
    case ARM::VST3d16Pseudo:
    case ARM::VST3d32Pseudo:
    case ARM::VST1d64TPseudo:
    case ARM::VST3d8Pseudo_UPD:
    case ARM::VST3d16Pseudo_UPD:
    case ARM::VST3d32Pseudo_UPD:
    case ARM::VST1d64TPseudo_UPD:
    case ARM::VST3q8Pseudo_UPD:
    case ARM::VST3q16Pseudo_UPD:
    case ARM::VST3q32Pseudo_UPD:
    case ARM::VST3q8oddPseudo:
    case ARM::VST3q16oddPseudo:
    case ARM::VST3q32oddPseudo:
    case ARM::VST3q8oddPseudo_UPD:
    case ARM::VST3q16oddPseudo_UPD:
    case ARM::VST3q32oddPseudo_UPD:
    case ARM::VST4d8Pseudo:
    case ARM::VST4d16Pseudo:
    case ARM::VST4d32Pseudo:
    case ARM::VST1d64QPseudo:
    case ARM::VST4d8Pseudo_UPD:
    case ARM::VST4d16Pseudo_UPD:
    case ARM::VST4d32Pseudo_UPD:
    case ARM::VST1d64QPseudo_UPD:
    case ARM::VST4q8Pseudo_UPD:
    case ARM::VST4q16Pseudo_UPD:
    case ARM::VST4q32Pseudo_UPD:
    case ARM::VST4q8oddPseudo:
    case ARM::VST4q16oddPseudo:
    case ARM::VST4q32oddPseudo:
    case ARM::VST4q8oddPseudo_UPD:
    case ARM::VST4q16oddPseudo_UPD:
    case ARM::VST4q32oddPseudo_UPD:
      ExpandVST(MBBI);
      return true;

    case ARM::VLD1LNq8Pseudo:
    case ARM::VLD1LNq16Pseudo:
    case ARM::VLD1LNq32Pseudo:
    case ARM::VLD1LNq8Pseudo_UPD:
    case ARM::VLD1LNq16Pseudo_UPD:
    case ARM::VLD1LNq32Pseudo_UPD:
    case ARM::VLD2LNd8Pseudo:
    case ARM::VLD2LNd16Pseudo:
    case ARM::VLD2LNd32Pseudo:
    case ARM::VLD2LNq16Pseudo:
    case ARM::VLD2LNq32Pseudo:
    case ARM::VLD2LNd8Pseudo_UPD:
    case ARM::VLD2LNd16Pseudo_UPD:
    case ARM::VLD2LNd32Pseudo_UPD:
    case ARM::VLD2LNq16Pseudo_UPD:
    case ARM::VLD2LNq32Pseudo_UPD:
    case ARM::VLD3LNd8Pseudo:
    case ARM::VLD3LNd16Pseudo:
    case ARM::VLD3LNd32Pseudo:
    case ARM::VLD3LNq16Pseudo:
    case ARM::VLD3LNq32Pseudo:
    case ARM::VLD3LNd8Pseudo_UPD:
    case ARM::VLD3LNd16Pseudo_UPD:
    case ARM::VLD3LNd32Pseudo_UPD:
    case ARM::VLD3LNq16Pseudo_UPD:
    case ARM::VLD3LNq32Pseudo_UPD:
    case ARM::VLD4LNd8Pseudo:
    case ARM::VLD4LNd16Pseudo:
    case ARM::VLD4LNd32Pseudo:
    case ARM::VLD4LNq16Pseudo:
    case ARM::VLD4LNq32Pseudo:
    case ARM::VLD4LNd8Pseudo_UPD:
    case ARM::VLD4LNd16Pseudo_UPD:
    case ARM::VLD4LNd32Pseudo_UPD:
    case ARM::VLD4LNq16Pseudo_UPD:
    case ARM::VLD4LNq32Pseudo_UPD:
    case ARM::VST1LNq8Pseudo:
    case ARM::VST1LNq16Pseudo:
    case ARM::VST1LNq32Pseudo:
    case ARM::VST1LNq8Pseudo_UPD:
    case ARM::VST1LNq16Pseudo_UPD:
    case ARM::VST1LNq32Pseudo_UPD:
    case ARM::VST2LNd8Pseudo:
    case ARM::VST2LNd16Pseudo:
    case ARM::VST2LNd32Pseudo:
    case ARM::VST2LNq16Pseudo:
    case ARM::VST2LNq32Pseudo:
    case ARM::VST2LNd8Pseudo_UPD:
    case ARM::VST2LNd16Pseudo_UPD:
    case ARM::VST2LNd32Pseudo_UPD:
    case ARM::VST2LNq16Pseudo_UPD:
    case ARM::VST2LNq32Pseudo_UPD:
    case ARM::VST3LNd8Pseudo:
    case ARM::VST3LNd16Pseudo:
    case ARM::VST3LNd32Pseudo:
    case ARM::VST3LNq16Pseudo:
    case ARM::VST3LNq32Pseudo:
    case ARM::VST3LNd8Pseudo_UPD:
    case ARM::VST3LNd16Pseudo_UPD:
    case ARM::VST3LNd32Pseudo_UPD:
    case ARM::VST3LNq16Pseudo_UPD:
    case ARM::VST3LNq32Pseudo_UPD:
    case ARM::VST4LNd8Pseudo:
    case ARM::VST4LNd16Pseudo:
    case ARM::VST4LNd32Pseudo:
    case ARM::VST4LNq16Pseudo:
    case ARM::VST4LNq32Pseudo:
    case ARM::VST4LNd8Pseudo_UPD:
    case ARM::VST4LNd16Pseudo_UPD:
    case ARM::VST4LNd32Pseudo_UPD:
    case ARM::VST4LNq16Pseudo_UPD:
    case ARM::VST4LNq32Pseudo_UPD:
      ExpandLaneOp(MBBI);
      return true;

    case ARM::VTBL2Pseudo: ExpandVTBL(MBBI, ARM::VTBL2, false, 2); return true;
    case ARM::VTBL3Pseudo: ExpandVTBL(MBBI, ARM::VTBL3, false, 3); return true;
    case ARM::VTBL4Pseudo: ExpandVTBL(MBBI, ARM::VTBL4, false, 4); return true;
    case ARM::VTBX2Pseudo: ExpandVTBL(MBBI, ARM::VTBX2, true, 2); return true;
    case ARM::VTBX3Pseudo: ExpandVTBL(MBBI, ARM::VTBX3, true, 3); return true;
    case ARM::VTBX4Pseudo: ExpandVTBL(MBBI, ARM::VTBX4, true, 4); return true;
  }

  return false;
}

bool ARMExpandPseudo::ExpandMBB(MachineBasicBlock &MBB) {
  bool Modified = false;

  MachineBasicBlock::iterator MBBI = MBB.begin(), E = MBB.end();
  while (MBBI != E) {
    MachineBasicBlock::iterator NMBBI = llvm::next(MBBI);
    Modified |= ExpandMI(MBB, MBBI);
    MBBI = NMBBI;
  }

  return Modified;
}

bool ARMExpandPseudo::runOnMachineFunction(MachineFunction &MF) {
  const TargetMachine &TM = MF.getTarget();
  TII = static_cast<const ARMBaseInstrInfo*>(TM.getInstrInfo());
  TRI = TM.getRegisterInfo();
  STI = &TM.getSubtarget<ARMSubtarget>();
  AFI = MF.getInfo<ARMFunctionInfo>();

  bool Modified = false;
  for (MachineFunction::iterator MFI = MF.begin(), E = MF.end(); MFI != E;
       ++MFI)
    Modified |= ExpandMBB(*MFI);
  if (VerifyARMPseudo)
    MF.verify(this, "After expanding ARM pseudo instructions.");
  return Modified;
}

/// createARMExpandPseudoPass - returns an instance of the pseudo instruction
/// expansion pass.
FunctionPass *llvm::createARMExpandPseudoPass() {
  return new ARMExpandPseudo();
}
