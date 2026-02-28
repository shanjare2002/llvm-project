#ifndef SIMPLIFYCFG_WITNESS_H
#define SIMPLIFYCFG_WITNESS_H

#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/PassManager.h"

#include "CFGMappingWitness.h"
#include "WitnessChecker.h"

struct SimplifyCFGWitness : public llvm::PassInfoMixin<SimplifyCFGWitness> {

  using StringVec = llvm::SmallVector<std::string, 4>;

  llvm::PreservedAnalyses run(llvm::Function &F,
                              llvm::FunctionAnalysisManager &FAM);

private:
  void generateWitness(
      llvm::Function &F, llvm::CFGMappingWitness &Witness,
      llvm::StringMap<StringVec> &witnessOneToMany,
      llvm::ArrayRef<std::string> deadNames, z3::context &SymCtx);
};

#endif
