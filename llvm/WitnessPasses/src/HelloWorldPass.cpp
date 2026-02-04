#include "llvm/Passes/PassPlugin.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Support/raw_ostream.h"
#include "HelloWorld.h"

using namespace llvm;

    PreservedAnalyses run(Function &F, FunctionAnalysisManager &) {
        errs() << "Hello World from function: " << F.getName() << "\n";
        
        return PreservedAnalyses::all();


    }
