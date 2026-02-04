#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/MDBuilder.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Utils/AssumeBundleBuilder.h"
#include "llvm/Transforms/Utils/Local.h"

#include "AcslDCE.h"
#include "z3++.h"

#define DEBUG_TYPE "acsl-dce"

using namespace llvm;

namespace {

static bool dceInstruction(Instruction *I,
                           SmallSetVector<Instruction *, 16> &WorkList,
                           const TargetLibraryInfo *TLI) {
  if (!isInstructionTriviallyDead(I, TLI))
    return false;

  salvageDebugInfo(*I);
  salvageKnowledge(I);

  for (unsigned Idx = 0, End = I->getNumOperands(); Idx != End; ++Idx) {
    Value *OpV = I->getOperand(Idx);
    I->setOperand(Idx, nullptr);

    if (!OpV->use_empty() || I == OpV)
      continue;

    if (auto *OpI = dyn_cast<Instruction>(OpV))
      if (isInstructionTriviallyDead(OpI, TLI))
        WorkList.insert(OpI);
  }

  I->eraseFromParent();
  return true;
}

static bool eliminateDeadCode(Function &F, const TargetLibraryInfo *TLI) {
  bool MadeChange = false;
  SmallSetVector<Instruction *, 16> WorkList;

  for (Instruction &I : make_early_inc_range(instructions(F))) {
    if (!WorkList.count(&I))
      MadeChange |= dceInstruction(&I, WorkList, TLI);
  }

  while (!WorkList.empty()) {
    Instruction *I = WorkList.pop_back_val();
    MadeChange |= dceInstruction(I, WorkList, TLI);
  }

  return MadeChange;
}

} // end anonymous namespace

void printBlockOrder(Function *F, StringRef label) {
  errs() << "=== Block pointers (" << label << ") ===\n";
  for (BasicBlock &BB : *F) {
    errs() << BB.getName() << " @ " << &BB << "\n";
  }
}

PreservedAnalyses AcslDCEPass::run(Function &F, FunctionAnalysisManager &AM) {
  errs() << "Running ACSL DCE on function: " << F.getName() << "\n";
  z3::context *C = InvariantManager::globalContext();
  WitnessChecker wC(&F, C);
  printBlockOrder(&F, "Source");

  const TargetLibraryInfo *TLI = &AM.getResult<TargetLibraryAnalysis>(F);
  bool Changed = eliminateDeadCode(F, TLI);

  printBlockOrder(&F, "TARGET");
  wC.buildBlockRelation(true);
  generateWitness(F, wC);

  errs() << "Witness check: ";

  wC.checkWitness();
  wC.dumpExpressions();
  assert(wC.checkWitness() && "Witness check failed!");
  wC.propagateInvariants();

  return Changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}

void AcslDCEPass::generateWitness(Function &F, WitnessChecker &wC) {

  z3::context *C = InvariantManager::globalContext();
  z3::expr WitnessExprST = C->bool_val(false);
  z3::expr WitnessExprVU = C->bool_val(false);
  z3::expr piS = C->int_const(concatStrings(STATE_S, PC_VAR).data());
  z3::expr piT = C->int_const(concatStrings(STATE_T, PC_VAR).data());
  z3::expr piU = C->int_const(concatStrings(STATE_U, PC_VAR).data());
  z3::expr piV = C->int_const(concatStrings(STATE_V, PC_VAR).data());

  for (auto &Blk : F) {
    int blockLabel = wC.getBlockLabel(&Blk);

    z3::expr WST =
        piS == blockLabel && piT == blockLabel &&
        createEqList(C, wC.getSourceVars(), STATE_S, STATE_T, PC_VAR);

    z3::expr WVU =
        piV == blockLabel && piU == blockLabel &&
        createEqList(C, wC.getTargetVars(), STATE_V, STATE_U, PC_VAR);

    WitnessExprST = WitnessExprST || WST;
    WitnessExprVU = WitnessExprVU || WVU;
  }

  z3::expr FinalST =
      piS == -1 && piT == -1 &&
      createEqList(C, wC.getTargetVars(), STATE_S, STATE_T, PC_VAR);

  z3::expr FinalVU =
      piV == -1 && piU == -1 &&
      createEqList(C, wC.getTargetVars(), STATE_V, STATE_U, PC_VAR);

  auto *Inv = InvariantManager::find(&*(--F.end()));
  if (Inv) {
    FinalST = FinalST && Inv->getInv(0);
    FinalVU = FinalVU && Inv->getInv(1);
  }

  WitnessExprST = WitnessExprST || FinalST;
  WitnessExprVU = WitnessExprVU || FinalVU;

  wC.setWitnessST(WitnessExprST.simplify());
  wC.setWitnessVU(WitnessExprVU.simplify());
}

/*
Transition Relations:
  Rs (SourceRelation): (and (= S_π 0) (= U_x 1) (= U_y (+ 2 S_x)) (= U_π -1))
  Rt (TargetRelation): (and (= T_π 0) (= V_x 1) (= V_y (+ 2 T_x)) (= V_π -1))

Witness Relations:
  W(S,T): (or (and (= S_π 0) (= T_π 0) (= S_x T_x) (= S_y T_y))
              (and (= S_π -1) (= T_π -1) (= S_x T_x) (= S_y T_y)))

  W(V,U): (or (and (= V_π 0) (= U_π 0) (= V_x U_x) (= V_y U_y))
              (and (= V_π -1) (= U_π -1) (= V_x U_x) (= V_y U_y)))
*/
