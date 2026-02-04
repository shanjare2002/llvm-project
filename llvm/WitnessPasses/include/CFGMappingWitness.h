#ifndef CFG_MAPPING_WITNESS_H
#define CFG_MAPPING_WITNESS_H

#include "z3++.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Value.h"
#include "llvm/Support/raw_ostream.h"

#include <map>
#include <string>

namespace llvm {

// Type alias for Z3 block value mappings: {block: {value: z3-expr}}
using Z3BlockMap =
    std::map<const BasicBlock *, std::map<const Value *, z3::expr>>;

// Type alias for string block value mappings: {block: {value: string-expr}}
using StringBlockMap =
    std::map<const BasicBlock *, std::map<const Value *, std::string>>;

/// CFGMappingWitness: Captures source and target CFG state before/after
/// SimplifyCFG transformation using static methods
class CFGMappingWitness {
public:
  /// Initialize the witness for a given function
  static void initialize(Function *F, z3::context *C);

  /// Build the source state mappings before transformation
  static void buildSourceMappings();

  /// Build the target state mappings after transformation
  static void buildTargetMappings();

  /// Dump source mappings in human-readable format
  static void
  dumpSourceStringMappings(const char *Label = "Source String Mappings");
  static void dumpSourceZ3Mappings(const char *Label = "Source Z3 Mappings");

  /// Dump target mappings in human-readable format
  static void
  dumpTargetStringMappings(const char *Label = "Target String Mappings");
  static void dumpTargetZ3Mappings(const char *Label = "Target Z3 Mappings");

  // Public accessors for mappings
  static const StringBlockMap &getSourceStringMap() { return SrcStringMap; }
  static const StringBlockMap &getTargetStringMap() { return TgtStringMap; }
  static const Z3BlockMap &getSourceZ3Map() { return SrcZ3Map; }
  static const Z3BlockMap &getTargetZ3Map() { return TgtZ3Map; }

  // Public accessors for per-block values
  static const std::map<const Value *, std::string> &
  getSourceBlockValues(const BasicBlock *BB);

  static const std::map<const Value *, std::string> &
  getTargetBlockValues(const BasicBlock *BB);

private:
  static Function *F;
  static z3::context *SymCtx;

  // String representation mappings
  static StringBlockMap SrcStringMap;
  static StringBlockMap TgtStringMap;

  // Z3 symbolic mappings
  static Z3BlockMap SrcZ3Map;
  static Z3BlockMap TgtZ3Map;

  // Per-block value collections (for debugging/analysis)
  static std::map<const BasicBlock *, std::map<const Value *, std::string>>
      SrcBlockValues;
  static std::map<const BasicBlock *, std::map<const Value *, std::string>>
      TgtBlockValues;

  // Helper functions
  static Z3BlockMap buildBlockZ3ValueMap();
  static StringBlockMap buildBlockStringValueMap();

  // Static helper functions for expression building
  static std::string getValueName(const Value *V);
  static std::string escapeValueString(llvm::StringRef In);
  static std::string getConstString(const ConstantInt *CI);
  static std::string
  getExprForValue(const Value *V, DenseMap<const Value *, std::string> &ValMap);
  static std::string
  computeInstructionExprString(const Instruction &I,
                               DenseMap<const Value *, std::string> &ValMap);

  static z3::expr makeFreshZ3Expr(const Value *V, z3::context &C);
  static z3::expr z3GetExprForValue(const Value *V, z3::context &C,
                                    std::map<const Value *, z3::expr> &ValMap);
  static z3::expr
  computeInstructionExprZ3(const Instruction &I, z3::context &C,
                           std::map<const Value *, z3::expr> &ValMap);
};

} // namespace llvm

#endif
