#include "GVNMappingWitness.h"

#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/Support/raw_ostream.h"

#include <map>
#include <set>
#include <string>

using namespace llvm;

// Static member definitions
Function *GVNMappingWitness::F = nullptr;
z3::context *GVNMappingWitness::SymCtx = nullptr;
GVNSymMap GVNMappingWitness::SrcMap;
GVNSymMap GVNMappingWitness::TgtMap;
std::optional<z3::expr> GVNMappingWitness::SrcRetExpr;
std::optional<z3::expr> GVNMappingWitness::TgtRetExpr;
std::optional<z3::expr> GVNMappingWitness::SrcAssumeConstraints;

// ---------------------------------------------------------------------------
// GVNMemState
// ---------------------------------------------------------------------------

void GVNMemState::applyStore(const z3::expr &Addr, const z3::expr &Val) {
  std::string NextName = "mem" + std::to_string(++MemVersion);
  z3::expr NextMem =
      C->constant(NextName.c_str(), C->array_sort(C->int_sort(), C->int_sort()));
  // NextMem == store(CurrentMem, Addr, Val) — we don't assert this as a Z3
  // constraint here; instead, we track the array symbolically using Z3's
  // built-in store/select. We DO update CurrentMem to the store expression so
  // that subsequent loads from the same address see the new value.
  CurrentMem = z3::store(CurrentMem, Addr, Val);
  (void)NextMem; // CurrentMem is the store expression directly
}

z3::expr GVNMemState::applyLoad(const z3::expr &Addr) const {
  return z3::select(CurrentMem, Addr);
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static std::string gvnGetValueName(const Value *V) {
  if (V->hasName())
    return V->getName().str();
  std::string S;
  raw_string_ostream OS(S);
  OS << "v" << (const void *)V;
  return OS.str();
}

// Retrieve a Z3 expression for V from the value map, or make a fresh one.
static z3::expr z3GetVal(const Value *V, z3::context &C,
                          std::map<const Value *, z3::expr> &VM) {
  auto It = VM.find(V);
  if (It != VM.end())
    return It->second;
  if (const auto *CI = dyn_cast<ConstantInt>(V))
    return C.int_val((int64_t)CI->getSExtValue());
  // Fresh uninterpreted constant for anything else not yet computed.
  std::string Name = gvnGetValueName(V);
  if (V->getType()->isIntegerTy(1))
    return C.bool_const(Name.c_str());
  return C.int_const(Name.c_str());
}

// ---------------------------------------------------------------------------
// GVNMappingWitness::computeZ3Expr
// ---------------------------------------------------------------------------

z3::expr GVNMappingWitness::computeZ3Expr(
    const Instruction &I, std::map<const Value *, z3::expr> &VM,
    GVNMemState &Mem) {
  z3::context &C = *SymCtx;

  // --- Load ---
  if (const auto *LI = dyn_cast<LoadInst>(&I)) {
    z3::expr Addr = z3GetVal(LI->getPointerOperand(), C, VM);
    return Mem.applyLoad(Addr);
  }

  // --- Store (void, but updates memory) ---
  if (const auto *SI = dyn_cast<StoreInst>(&I)) {
    z3::expr Addr = z3GetVal(SI->getPointerOperand(), C, VM);
    z3::expr Val  = z3GetVal(SI->getValueOperand(), C, VM);
    Mem.applyStore(Addr, Val);
    return C.int_val(0); // stores don't produce a value
  }

  // --- PHI node: encode as nested ite over incoming values ---
  if (const auto *PHI = dyn_cast<PHINode>(&I)) {
    unsigned N = PHI->getNumIncomingValues();
    if (N == 0)
      return makeFreshExpr(&I);
    // Build ite(cond_bb0, val0, ite(cond_bb1, val1, ... valN-1))
    // Use fresh boolean variables for "came from pred_k" conditions.
    z3::expr Result = z3GetVal(PHI->getIncomingValue(N - 1), C, VM);
    for (int k = (int)N - 2; k >= 0; --k) {
      BasicBlock *PredBB = PHI->getIncomingBlock(k);
      std::string CondName =
          "from_" +
          (PredBB->hasName() ? PredBB->getName().str()
                             : ("bb" + std::to_string(k)));
      z3::expr Cond = C.bool_const(CondName.c_str());
      z3::expr Val  = z3GetVal(PHI->getIncomingValue(k), C, VM);
      // Ensure both sides have compatible sorts
      if (Val.is_bool() != Result.is_bool()) {
        // Fall back to fresh variable on sort mismatch
        return makeFreshExpr(&I);
      }
      Result = z3::ite(Cond, Val, Result);
    }
    return Result;
  }

  // --- Binary operators ---
  if (const auto *BO = dyn_cast<BinaryOperator>(&I)) {
    z3::expr L = z3GetVal(BO->getOperand(0), C, VM);
    z3::expr R = z3GetVal(BO->getOperand(1), C, VM);
    // Require integer sorts for arithmetic.
    if (!L.is_int() || !R.is_int())
      return makeFreshExpr(&I);
    switch (BO->getOpcode()) {
    case Instruction::Add:  return L + R;
    case Instruction::Sub:  return L - R;
    case Instruction::Mul:  return L * R;
    case Instruction::SDiv: return L / R;
    case Instruction::UDiv: return L / R; // approximate (ignore sign)
    case Instruction::SRem: return z3::mod(L, R);
    case Instruction::URem: return z3::mod(L, R);
    // Bitwise ops require bitvectors — return fresh variable for integer model
    case Instruction::And:
    case Instruction::Or:
    case Instruction::Xor:
    case Instruction::Shl:
    case Instruction::LShr:
    case Instruction::AShr:
      return makeFreshExpr(&I);
    default:
      return makeFreshExpr(&I);
    }
  }

  // --- Comparison ---
  if (const auto *CI = dyn_cast<ICmpInst>(&I)) {
    z3::expr L = z3GetVal(CI->getOperand(0), C, VM);
    z3::expr R = z3GetVal(CI->getOperand(1), C, VM);
    if (!L.is_int() || !R.is_int())
      return makeFreshExpr(&I);
    switch (CI->getPredicate()) {
    case CmpInst::ICMP_EQ:  return L == R;
    case CmpInst::ICMP_NE:  return L != R;
    case CmpInst::ICMP_SLT: return L < R;
    case CmpInst::ICMP_SLE: return L <= R;
    case CmpInst::ICMP_SGT: return L > R;
    case CmpInst::ICMP_SGE: return L >= R;
    // Unsigned comparisons — approximate as signed for integer model
    case CmpInst::ICMP_ULT: return L < R;
    case CmpInst::ICMP_ULE: return L <= R;
    case CmpInst::ICMP_UGT: return L > R;
    case CmpInst::ICMP_UGE: return L >= R;
    default:
      return makeFreshExpr(&I);
    }
  }

  // --- Select ---
  if (const auto *SI = dyn_cast<SelectInst>(&I)) {
    z3::expr Cond = z3GetVal(SI->getCondition(), C, VM);
    z3::expr TV   = z3GetVal(SI->getTrueValue(), C, VM);
    z3::expr FV   = z3GetVal(SI->getFalseValue(), C, VM);
    if (!Cond.is_bool())
      return makeFreshExpr(&I);
    if (TV.is_bool() != FV.is_bool())
      return makeFreshExpr(&I);
    return z3::ite(Cond, TV, FV);
  }

  // --- Cast instructions: for integer casts treat as identity ---
  if (const auto *Cast = dyn_cast<CastInst>(&I)) {
    z3::expr Src = z3GetVal(Cast->getOperand(0), C, VM);
    switch (Cast->getOpcode()) {
    case Instruction::ZExt:
    case Instruction::SExt:
    case Instruction::Trunc:
      // Approximate: treat as identity (lose precision info but safe for EQ checks)
      if (Src.is_int())
        return Src;
      return makeFreshExpr(&I);
    case Instruction::BitCast:
      return Src; // same bits, different type
    default:
      return makeFreshExpr(&I);
    }
  }

  // --- GEP: model address as an integer expression ---
  if (const auto *GEP = dyn_cast<GetElementPtrInst>(&I)) {
    z3::expr Base = z3GetVal(GEP->getPointerOperand(), C, VM);
    z3::expr Addr = Base;
    for (const Use &Idx : GEP->indices()) {
      z3::expr IdxExpr = z3GetVal(Idx.get(), C, VM);
      if (Addr.is_int() && IdxExpr.is_int())
        Addr = Addr + IdxExpr;
      else
        return makeFreshExpr(&I);
    }
    return Addr;
  }

  // --- Freeze: treat as identity ---
  if (const auto *FI = dyn_cast<FreezeInst>(&I)) {
    return z3GetVal(FI->getOperand(0), C, VM);
  }

  // Unhandled: return a fresh uninterpreted constant.
  return makeFreshExpr(&I);
}

// ---------------------------------------------------------------------------
// GVNMappingWitness::makeFreshExpr
// ---------------------------------------------------------------------------

z3::expr GVNMappingWitness::makeFreshExpr(const Value *V) {
  std::string Name = gvnGetValueName(V);
  if (V->getType()->isIntegerTy(1))
    return SymCtx->bool_const(Name.c_str());
  return SymCtx->int_const(Name.c_str());
}

// ---------------------------------------------------------------------------
// GVNMappingWitness::buildMappings
// ---------------------------------------------------------------------------

void GVNMappingWitness::buildMappings(bool isSrc) {
  z3::context &C = *SymCtx;
  GVNSymMap &Out = isSrc ? SrcMap : TgtMap;
  std::optional<z3::expr> &RetOut = isSrc ? SrcRetExpr : TgtRetExpr;
  Out.clear();
  RetOut.reset();

  if (isSrc)
    SrcAssumeConstraints.reset();

  // Value map: Value* -> Z3 expr (valid during build, pointer keys are safe
  // here because we don't run any transformation in between).
  std::map<const Value *, z3::expr> VM;

  // Seed with function arguments.
  for (Argument &A : F->args()) {
    z3::expr E = makeFreshExpr(&A);
    VM.insert_or_assign(&A, E);
    Out.insert_or_assign(gvnGetValueName(&A), E);
  }

  // Single shared memory state across the whole function.
  GVNMemState Mem(C);

  // Accumulated assume constraints (source scan only).
  // Each llvm.assume(cond) in block B adds: from_B → cond_expr.
  // This lets the return-value VC account for runtime preconditions.
  z3::expr AssumeConj = C.bool_val(true);

  // Walk all basic blocks in IR order.
  for (BasicBlock &BB : *F) {
    // The "block-entry condition" for block B is represented by the bool const
    // "from_<B>" — the same name used in PHI ite-expressions for incoming
    // values from B.  This is a sound approximation: any assume that fires
    // inside B can be stated as "if we took an edge into this block's
    // successor FROM B, then cond holds".
    std::string BName = BB.hasName() ? BB.getName().str() : std::string("(unnamed)");
    z3::expr BlockCond = C.bool_const(("from_" + BName).c_str());

    for (Instruction &I : BB) {
      // ── llvm.assume: collect path-conditional constraints ──────────────
      if (isSrc) {
        if (const auto *II = dyn_cast<IntrinsicInst>(&I)) {
          if (II->getIntrinsicID() == Intrinsic::assume) {
            Value *Arg = II->getArgOperand(0);
            auto CondIt = VM.find(Arg);
            if (CondIt != VM.end() && CondIt->second.is_bool()) {
              // from_B → cond
              AssumeConj = AssumeConj && z3::implies(BlockCond, CondIt->second);
            }
          }
        }
      }

      if (I.getType()->isVoidTy()) {
        if (isa<StoreInst>(&I))
          computeZ3Expr(I, VM, Mem);
        continue;
      }

      z3::expr E = computeZ3Expr(I, VM, Mem);
      VM.insert_or_assign(&I, E);

      std::string Name = gvnGetValueName(&I);
      // Skip anonymous values (pointer-address names like "v0x...") for the
      // output map; we keep them in VM for operand resolution.
      if (Name.rfind("v0x", 0) != 0)
        Out.insert_or_assign(Name, E);
    }

    // Capture return value.
    if (auto *RI = dyn_cast<ReturnInst>(BB.getTerminator())) {
      if (Value *RV = RI->getReturnValue()) {
        RetOut = z3GetVal(RV, C, VM);
      }
    }
  }

  if (isSrc)
    SrcAssumeConstraints = AssumeConj;
}

// ---------------------------------------------------------------------------
// GVNMappingWitness::initialize / clear / dump
// ---------------------------------------------------------------------------

void GVNMappingWitness::initialize(Function *Func, z3::context *C) {
  F = Func;
  SymCtx = C;
  clear();
}

void GVNMappingWitness::clear() {
  SrcMap.clear();
  TgtMap.clear();
  SrcRetExpr.reset();
  TgtRetExpr.reset();
  SrcAssumeConstraints.reset();
}

void GVNMappingWitness::dump(const char *Label, const GVNSymMap &M) {
  errs() << "\n=== " << Label << " ===\n";
  for (const auto &[Name, E] : M)
    errs() << "  " << Name << " = "
           << Z3_ast_to_string(*SymCtx, E) << "\n";
}
