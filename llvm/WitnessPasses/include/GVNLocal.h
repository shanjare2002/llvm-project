//===- GVNLocal.h - Local GVN wrapper ---------------------------*- C++ -*-===//
//
// This header provides a locally-named GVN pass for debugging and a set of
// global vectors that the pass populates during execution so that the
// GVNWitnessPass can verify the transformation.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_WITNESSPASSES_GVNLOCAL_H
#define LLVM_WITNESSPASSES_GVNLOCAL_H

#include <string>
#include <utility>
#include <vector>

/// Every value replacement GVN makes is recorded here as (old_name, new_name).
/// "old replaced by new" means old and new must be equal at that point.
extern std::vector<std::pair<std::string, std::string>> GVNReplacements;

/// Every critical-edge split GVN performs for PRE.
/// Entry = (pred_block_name, new_intermediate_block_name).
extern std::vector<std::pair<std::string, std::string>> GVNCFGChanges;

#define GVNPass GVNPassLocal
#include "llvm/Transforms/Scalar/GVN.h"
#undef GVNPass

#endif
