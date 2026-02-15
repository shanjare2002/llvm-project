#ifndef SIMPLIFYCFG_WITNESS_H
#define SIMPLIFYCFG_WITNESS_H

#include "llvm/IR/Function.h"
#include "llvm/IR/PassManager.h"

#include "llvm/ADT/DenseMap.h"

#include "CFGMappingWitness.h"
#include "WitnessChecker.h"

struct SimplifyCFGWitness : public llvm::PassInfoMixin<SimplifyCFGWitness> {

  llvm::PreservedAnalyses run(llvm::Function &F,
                              llvm::FunctionAnalysisManager &FAM);

private:
  void generateWitness(
      llvm::Function &F, llvm::CFGMappingWitness &Witness,
      llvm::DenseMap<llvm::BasicBlock *, llvm::BasicBlock *> &BlockMap,
      z3::context &SymCtx);
};

#endif
