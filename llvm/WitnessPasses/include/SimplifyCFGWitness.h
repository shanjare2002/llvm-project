#ifndef SIMPLIFYCFG_WITNESS_H
#define SIMPLIFYCFG_WITNESS_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/PassManager.h"

#include "CFGMappingWitness.h"
#include "WitnessChecker.h"

struct SimplifyCFGWitness : public llvm::PassInfoMixin<SimplifyCFGWitness> {

  using StringRefVec = llvm::SmallVector<llvm::StringRef, 4>;

  llvm::PreservedAnalyses run(llvm::Function &F,
                              llvm::FunctionAnalysisManager &FAM);

private:
  void generateWitness(
      llvm::Function &F, llvm::CFGMappingWitness &Witness,
      llvm::DenseMap<llvm::StringRef, StringRefVec> &witnessOneToMany,
      llvm::ArrayRef<std::string> deadNames, z3::context &SymCtx);
};

#endif
