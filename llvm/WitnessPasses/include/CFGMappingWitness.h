#ifndef CFG_MAPPING_WITNESS_H
#define CFG_MAPPING_WITNESS_H

#include "z3++.h"

#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Value.h"
#include "llvm/Support/raw_ostream.h"

#include <map>
#include <string>
#include <unordered_map>

namespace llvm {

// Hash function for const Value*
struct ValuePtrHash {
  std::size_t operator()(const Value *V) const {
    return std::hash<const void *>()(V);
  }
};

// Equality for const Value*
struct ValuePtrEqual {
  bool operator()(const Value *a, const Value *b) const { return a == b; }
};

// Hash function for const BasicBlock*
struct BasicBlockPtrHash {
  std::size_t operator()(const BasicBlock *BB) const {
    return std::hash<const void *>()(BB);
  }
};

// Equality for const BasicBlock*
struct BasicBlockPtrEqual {
  bool operator()(const BasicBlock *a, const BasicBlock *b) const {
    return a == b;
  }
};

// Type alias for Z3 block value mappings: {block_name: {value: z3-expr}}
using Z3BlockMap = std::map<std::string, std::map<const Value *, z3::expr>>;

// Type alias for string block value mappings: {block_name: {value:
// string-expr}}
using StringBlockMap =
    std::map<std::string, std::map<const Value *, std::string>>;

// Type alias for PHI incoming mappings: {phi_name: {incoming_block: expr}}
using PhiIncomingMap = std::map<std::string, std::map<std::string, z3::expr>>;
// Type alias for return value mappings: {block_name: expr}
using RetValueMap = std::map<std::string, z3::expr>;
// Type alias for return PHI mappings: {block_name: phi_name}
using RetPhiNameMap = std::map<std::string, std::string>;
// Type alias for path condition mappings: {block_name: expr}
using PathCondMap = std::map<std::string, z3::expr>;

using StringRefVec = SmallVector<StringRef, 4>;

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

  /// Clear all cached mappings (must be called before Z3 context teardown)
  static void clear();

  /// Access the Z3 context used for cached mappings.
  static z3::context &getContext();

  /// Dump source mappings in human-readable format
  static void
  dumpSourceStringMappings(const char *Label = "Source String Mappings");
  static void dumpSourceZ3Mappings(const char *Label = "Source Z3 Mappings");
  static void
  dumpSourceZ3MapEntries(const char *Label = "Source Z3 Map Entries");
  static void dumpSourcePhiMappings(const char *Label = "Source PHI Mappings");

  /// Dump target mappings in human-readable format
  static void
  dumpTargetStringMappings(const char *Label = "Target String Mappings");
  static void dumpTargetZ3Mappings(const char *Label = "Target Z3 Mappings");
  static void
  dumpTargetZ3MapEntries(const char *Label = "Target Z3 Map Entries");
  static void dumpTargetPhiMappings(const char *Label = "Target PHI Mappings");

  // Public accessors for mappings
  static const StringBlockMap &getSourceStringMap() { return SrcStringMap; }
  static const StringBlockMap &getTargetStringMap() { return TgtStringMap; }
  static const Z3BlockMap &getSourceZ3Map() { return SrcZ3Map; }
  static const Z3BlockMap &getTargetZ3Map() { return TgtZ3Map; }
  static const PhiIncomingMap &getSourcePhiMap() { return SrcPhiMap; }
  static const PhiIncomingMap &getTargetPhiMap() { return TgtPhiMap; }
  static const RetValueMap &getSourceRetMap() { return SrcRetMap; }
  static const RetValueMap &getTargetRetMap() { return TgtRetMap; }
  static const RetPhiNameMap &getSourceRetPhiMap() { return SrcRetPhiMap; }
  static const RetPhiNameMap &getTargetRetPhiMap() { return TgtRetPhiMap; }
  static const PathCondMap &getSourcePathCondMap() { return SrcPathCondMap; }
  static const PathCondMap &getTargetPathCondMap() { return TgtPathCondMap; }

  // Public accessors for per-block values
  static const std::map<const Value *, std::string> &
  getSourceBlockValues(const BasicBlock *BB);

  static const std::map<const Value *, std::string> &
  getTargetBlockValues(const BasicBlock *BB);

  /// Build state implication using aggregated variables from block lists.
  /// Uses cached source/target block mappings and preserves equality between
  /// common variables.
  static z3::expr buildCommonVariableEquality(z3::context &C,
                                              const StringRefVec &src_blocks,
                                              const StringRefVec &tgt_blocks);

private:
  static Function *F;
  static z3::context *SymCtx;

  // String representation mappings
  static StringBlockMap SrcStringMap;
  static StringBlockMap TgtStringMap;

  // Z3 symbolic mappings
  static Z3BlockMap SrcZ3Map;
  static Z3BlockMap TgtZ3Map;
  static PhiIncomingMap SrcPhiMap;
  static PhiIncomingMap TgtPhiMap;
  static RetValueMap SrcRetMap;
  static RetValueMap TgtRetMap;
  static RetPhiNameMap SrcRetPhiMap;
  static RetPhiNameMap TgtRetPhiMap;
  static PathCondMap SrcPathCondMap;
  static PathCondMap TgtPathCondMap;

  // Per-block value collections (for debugging/analysis)
  static std::map<const BasicBlock *, std::map<const Value *, std::string>>
      SrcBlockValues;
  static std::map<const BasicBlock *, std::map<const Value *, std::string>>
      TgtBlockValues;

  // Helper functions
  static Z3BlockMap buildBlockZ3ValueMap(PhiIncomingMap *PhiMap,
                                         RetValueMap *RetMap,
                                         RetPhiNameMap *RetPhiMap);
  static StringBlockMap buildBlockStringValueMap();
  static PathCondMap buildPathCondMap(const Z3BlockMap &Z3Map);

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
