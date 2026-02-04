
#ifndef HELLO_WORLD_H
#define HELLO_WORLD_H

#include "llvm/IR/PassManager.h"
#include "llvm/IR/Function.h"
#include "z3++.h"

struct HelloWorldPass : public llvm::PassInfoMixin<HelloWorldPass> {
    llvm::PreservedAnalyses run(llvm::Function &F, 
                                llvm::FunctionAnalysisManager &AM);
};

#endif