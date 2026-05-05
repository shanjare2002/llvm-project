#ifndef LLVM_WITNESS_PASSES_GVNLOCAL_H
#define LLVM_WITNESS_PASSES_GVNLOCAL_H

#include <string>
#include <utility>
#include <vector>

/// Every value replacement GVN makes: (old_name, new_name).
extern std::vector<std::pair<std::string, std::string>> GVNReplacements;

/// Every critical-edge split GVN performs for PRE: (pred_name, new_block_name).
extern std::vector<std::pair<std::string, std::string>> GVNCFGChanges;

#define GVNPass GVNPassLocal
#define GVNLegacyPass GVNLegacyPassLocal
#define createGVNPass createGVNPassLocal
#include "llvm/Transforms/Scalar/GVN.h"
#undef createGVNPass
#undef GVNLegacyPass
#undef GVNPass

#endif
