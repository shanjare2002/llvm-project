#include "z3++.h"

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
#include "llvm/Transforms/Scalar/DeadStoreElimination.h"
#include "llvm/Transforms/Utils/AssumeBundleBuilder.h"
#include "llvm/Transforms/Utils/Local.h"

#include "DSEWitnessPass.h"

#define DEBUG_TYPE "DSEWitnessPass"

using namespace llvm;

PreservedAnalyses DSEWitnessPass::run(Function &F,
                                      FunctionAnalysisManager &FAM) {
  DSEPass dsePass;
  z3::context *C = InvariantManager::globalContext();
  WitnessChecker wC(&F, C);
  const TargetLibraryInfo *TLI = &FAM.getResult<TargetLibraryAnalysis>(F);
  PreservedAnalyses PA = dsePass.run(F, FAM);

  wC.buildBlockRelation(true);
  generateWitness(F, wC);
  errs() << "Witness check: ";

  wC.checkWitness();
  wC.dumpExpressions();
  assert(wC.checkWitness() && "Witness check failed!");
  wC.propagateInvariants();

  return PA;
}

void DSEWitnessPass::generateWitness(Function &F, WitnessChecker &wC) {
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
        createEqList(C, wC.getTargetVars(), STATE_S, STATE_T, PC_VAR);

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