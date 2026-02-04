#include "llvm/Passes/PassPlugin.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

// A simple function pass that prints "Hello World" for each function
struct HelloWorldPass : PassInfoMixin<HelloWorldPass> {
    PreservedAnalyses run(Function &F, FunctionAnalysisManager &) {
        errs() << "Hello World from function: " << F.getName() << "\n";
        return PreservedAnalyses::all();
    }
};

// Plugin entry point
llvm::PassPluginLibraryInfo getHelloWorldVerifierPluginInfo() {
    return {LLVM_PLUGIN_API_VERSION, "HelloWorldVerifier", LLVM_VERSION_STRING,
        [](PassBuilder &PB) {
            PB.registerPipelineParsingCallback(
                [](StringRef Name, FunctionPassManager &FPM,
                   ArrayRef<PassBuilder::PipelineElement>) {
                    if (Name == "hello-world") {
                        FPM.addPass(HelloWorldPass());
                        return true;
                    }
                    return false;
                });
        }};
}

extern "C" ::llvm::PassPluginLibraryInfo llvmGetPassPluginInfo() LLVM_ATTRIBUTE_WEAK {
    return getHelloWorldVerifierPluginInfo();
}
