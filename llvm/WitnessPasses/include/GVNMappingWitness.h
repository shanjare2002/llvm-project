#ifndef GVN_MAPPING_WITNESS_H
#define GVN_MAPPING_WITNESS_H

#include "z3++.h"
#include "llvm/IR/Function.h"
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace llvm {
class Value;
class Instruction;
} // namespace llvm

// Maps variable name -> Z3 symbolic expression (per-function, flat — GVN
// doesn't change control flow so we don't need a per-block split).
using GVNSymMap = std::map<std::string, z3::expr>;

// Memory state: tracks a symbolic array representing heap/stack memory.
// Each store creates a new named memory state; loads select from the current
// memory state. This gives a simple but sound abstraction for load/store.
struct GVNMemState {
  z3::context *C;
  int MemVersion = 0;
  z3::expr CurrentMem;

  explicit GVNMemState(z3::context &Ctx)
      : C(&Ctx), CurrentMem(Ctx.constant("mem0", Ctx.array_sort(
                                                      Ctx.int_sort(),
                                                      Ctx.int_sort()))) {}

  // Record a store: mem' = update(mem, addr, val)
  void applyStore(const z3::expr &Addr, const z3::expr &Val);

  // Compute a load: select(mem, addr)
  z3::expr applyLoad(const z3::expr &Addr) const;

  // Return the current memory expression
  const z3::expr &getMem() const { return CurrentMem; }
};

class GVNMappingWitness {
public:
  // Initialise for a new function + Z3 context.
  static void initialize(llvm::Function *F, z3::context *C);

  // Scan the function (before or after GVN) and build a flat symbolic map.
  // isSrc=true builds SrcMap / SrcRetExpr; false builds TgtMap / TgtRetExpr.
  static void buildMappings(bool isSrc);

  // Accessors
  static const GVNSymMap &getSrcMap() { return SrcMap; }
  static const GVNSymMap &getTgtMap() { return TgtMap; }
  static const z3::expr  &getSrcRet()  { return *SrcRetExpr; }
  static const z3::expr  &getTgtRet()  { return *TgtRetExpr; }
  static bool hasSrcRet() { return SrcRetExpr.has_value(); }
  static bool hasTgtRet() { return TgtRetExpr.has_value(); }
  static llvm::Function *getFunction() { return F; }
  static z3::context    &getContext()  { return *SymCtx; }
  /// Returns the conjunction of all assume-based constraints gathered during
  /// buildMappings(true) (the source scan).  Each assume(cond) in block B
  /// contributes "from_B → cond" — see GVNMappingWitness.cpp for details.
  static const z3::expr &getSrcAssumeConstraints() { return *SrcAssumeConstraints; }
  static bool hasSrcAssumeConstraints() { return SrcAssumeConstraints.has_value(); }

  // Make a fresh Z3 integer constant named after the LLVM value.
  static z3::expr makeFreshExpr(const llvm::Value *V);

  // Compute a Z3 expression for an instruction given a local value map.
  // Returns a fresh variable for unsupported operations.
  static z3::expr computeZ3Expr(const llvm::Instruction &I,
                                 std::map<const llvm::Value *, z3::expr> &VM,
                                 GVNMemState &Mem);

  static void clear();
  static void dump(const char *Label, const GVNSymMap &M);

private:
  static llvm::Function *F;
  static z3::context    *SymCtx;
  static GVNSymMap       SrcMap;
  static GVNSymMap       TgtMap;
  static std::optional<z3::expr> SrcRetExpr;
  static std::optional<z3::expr> TgtRetExpr;
  // Conjunction of all source-function assume constraints (path-guarded).
  static std::optional<z3::expr> SrcAssumeConstraints;
};

#endif // GVN_MAPPING_WITNESS_H
