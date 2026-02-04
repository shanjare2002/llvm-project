#ifndef ACSL_DCE_H
#define ACSL_DCE_H

#include "llvm/IR/PassManager.h"
#include "llvm/IR/Function.h"

#include "WitnessChecker.h"

struct AcslDCEPass : public llvm::PassInfoMixin<AcslDCEPass> {
    llvm::PreservedAnalyses run(llvm::Function &F, 
                                llvm::FunctionAnalysisManager &AM);
private:
    void generateWitness(llvm::Function &F, WitnessChecker &wC);
};

#endif