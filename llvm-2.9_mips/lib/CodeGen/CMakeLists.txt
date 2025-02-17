add_llvm_library(LLVMCodeGen
  AggressiveAntiDepBreaker.cpp
  AllocationOrder.cpp
  Analysis.cpp
  BranchFolding.cpp
  CalcSpillWeights.cpp
  CallingConvLower.cpp
  CodeGen.cpp
  CodePlacementOpt.cpp
  CriticalAntiDepBreaker.cpp
  DeadMachineInstructionElim.cpp
  DwarfEHPrepare.cpp
  EdgeBundles.cpp
  ELFCodeEmitter.cpp
  ELFWriter.cpp
  ExpandISelPseudos.cpp
  GCMetadata.cpp
  GCMetadataPrinter.cpp
  GCStrategy.cpp
  IfConversion.cpp
  InlineSpiller.cpp
  InterferenceCache.cpp
  IntrinsicLowering.cpp
  LLVMTargetMachine.cpp
  LatencyPriorityQueue.cpp
  LexicalScopes.cpp
  LiveDebugVariables.cpp
  LiveInterval.cpp
  LiveIntervalAnalysis.cpp
  LiveIntervalUnion.cpp
  LiveStackAnalysis.cpp
  LiveVariables.cpp
  LiveRangeEdit.cpp
  LocalStackSlotAllocation.cpp
  LowerSubregs.cpp
  MachineBasicBlock.cpp
  MachineBlockFrequencyInfo.cpp
  MachineBranchProbabilityInfo.cpp
  MachineCSE.cpp
  MachineDominators.cpp
  MachineFunction.cpp
  MachineFunctionAnalysis.cpp
  MachineFunctionPass.cpp
  MachineFunctionPrinterPass.cpp
  MachineInstr.cpp
  MachineLICM.cpp
  MachineLoopInfo.cpp
  MachineLoopRanges.cpp
  MachineModuleInfo.cpp
  MachineModuleInfoImpls.cpp
  MachinePassRegistry.cpp
  MachineRegisterInfo.cpp
  MachineSSAUpdater.cpp
  MachineSink.cpp
  MachineVerifier.cpp
  ObjectCodeEmitter.cpp
  OcamlGC.cpp
  OptimizePHIs.cpp
  PHIElimination.cpp
  PHIEliminationUtils.cpp
  Passes.cpp
  PeepholeOptimizer.cpp
  PostRASchedulerList.cpp
  ProcessImplicitDefs.cpp
  PrologEpilogInserter.cpp
  PseudoSourceValue.cpp
  RegAllocBasic.cpp
  RegAllocFast.cpp
  RegAllocGreedy.cpp
  RegAllocLinearScan.cpp
  RegAllocPBQP.cpp
  RegisterClassInfo.cpp
  RegisterCoalescer.cpp
  RegisterScavenging.cpp
  RenderMachineFunction.cpp
  ScheduleDAG.cpp
  ScheduleDAGEmit.cpp
  ScheduleDAGInstrs.cpp
  ScheduleDAGPrinter.cpp
  ScoreboardHazardRecognizer.cpp
  ShadowStackGC.cpp
  ShrinkWrapping.cpp
  SjLjEHPrepare.cpp
  SlotIndexes.cpp
  Spiller.cpp
  SpillPlacement.cpp
  SplitKit.cpp
  Splitter.cpp
  StackProtector.cpp
  StackSlotColoring.cpp
  StrongPHIElimination.cpp
  TailDuplication.cpp
  TargetInstrInfoImpl.cpp
  TargetLoweringObjectFileImpl.cpp
  TwoAddressInstructionPass.cpp
  UnreachableBlockElim.cpp
  VirtRegMap.cpp
  VirtRegRewriter.cpp
  )

add_llvm_library_dependencies(LLVMCodeGen
  LLVMAnalysis
  LLVMCore
  LLVMMC
  LLVMScalarOpts
  LLVMSupport
  LLVMTarget
  LLVMTransformUtils
  )

add_subdirectory(SelectionDAG)
add_subdirectory(AsmPrinter)
