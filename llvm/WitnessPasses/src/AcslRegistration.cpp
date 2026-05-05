#include "AcslDCE.h"
#include "DSEWitnessPass.h"
#include "GVNWitnessPass.h"
#include "HelloWorld.h"
#include "SimplifyCFGWitness.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Support/raw_ostream.h"
using namespace llvm;

// Forward declare your pass structs
struct HelloWorldPass;
struct AcslDCEPass;
struct DSEWitnessPass;
struct SimplifyCFGPass;
// Single registration point for all passes
extern "C" LLVM_ATTRIBUTE_WEAK ::llvm::PassPluginLibraryInfo
llvmGetPassPluginInfo() {
  return {LLVM_PLUGIN_API_VERSION, "WitnessPasses", LLVM_VERSION_STRING,
          [](PassBuilder &PB) {
            PB.registerPipelineParsingCallback(
                [](StringRef Name, FunctionPassManager &FPM,
                   ArrayRef<PassBuilder::PipelineElement>) {
                  if (Name == "hello-world") {
                    FPM.addPass(HelloWorldPass());
                    return true;
                  }
                  if (Name == "acsldce") {
                    FPM.addPass(AcslDCEPass());
                    return true;
                  }
                  if (Name == "simplifycfg-witness") {
                    FPM.addPass(SimplifyCFGWitness());
                    return true;
                  }
                  if (Name == "dseWitness") {
                    FPM.addPass(DSEWitnessPass());
                    return true;
                  }
                  if (Name == "gvn-witness") {
                    FPM.addPass(GVNWitnessPass());
                    return true;
                  }
                  return false;
                });
          }};
}