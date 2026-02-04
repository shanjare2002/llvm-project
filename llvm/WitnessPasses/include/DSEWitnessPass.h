#ifndef DSE_WITNESSPASS_H
#define DSE_WITNESSPASS_H

#include "llvm/IR/Function.h"
#include "llvm/IR/PassManager.h"

#include "WitnessChecker.h"

struct DSEWitnessPass : public llvm::PassInfoMixin<DSEWitnessPass> {

  llvm::PreservedAnalyses run(llvm::Function &F,
                              llvm::FunctionAnalysisManager &FAM);

private:
  void generateWitness(llvm::Function &F, WitnessChecker &wC);
};

#endif