#ifndef GVN_WITNESS_H
#define GVN_WITNESS_H

#include "llvm/IR/Function.h"
#include "llvm/IR/PassManager.h"

#include "WitnessChecker.h"

struct GVNWitness : public llvm::PassInfoMixin<GVNWitness> {

  llvm::PreservedAnalyses run(llvm::Function &F,
                              llvm::FunctionAnalysisManager &FAM);

private:
  void generateWitness(llvm::Function &F, WitnessChecker &wC);
};

#endif
