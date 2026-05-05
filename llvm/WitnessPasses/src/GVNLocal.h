#ifndef LLVM_WITNESS_PASSES_GVNLOCAL_H
#define LLVM_WITNESS_PASSES_GVNLOCAL_H

#define GVNPass GVNPassLocal
#define GVNLegacyPass GVNLegacyPassLocal
#define createGVNPass createGVNPassLocal
#include "llvm/Transforms/Scalar/GVN.h"
#undef createGVNPass
#undef GVNLegacyPass
#undef GVNPass

#include <vector>
#include <string>
#include "llvm/IR/Value.h"
#include "llvm/IR/Constants.h"

extern std::vector<std::pair<std::string, std::string>> GVNReplacements;

#endif
