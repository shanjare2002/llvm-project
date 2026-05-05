#ifndef GVN_WITNESS_PASS_H
#define GVN_WITNESS_PASS_H

#include "llvm/IR/Function.h"
#include "llvm/IR/PassManager.h"

/// GVN witness pass.
///
/// Principle: when GVN replaces OLD with NEW at a program point, the symbolic
/// expressions for OLD and NEW (computed from function arguments) must be
/// provably equal.  The pass also verifies that the return value is preserved.
struct GVNWitnessPass : public llvm::PassInfoMixin<GVNWitnessPass> {
  llvm::PreservedAnalyses run(llvm::Function &F,
                              llvm::FunctionAnalysisManager &FAM);
};

#endif // GVN_WITNESS_PASS_H
