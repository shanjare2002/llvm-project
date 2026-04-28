#include "CFGMappingWitness.h"

#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Support/raw_ostream.h"

#include <map>
#include <set>
#include <string>
#include <unordered_map>

using namespace llvm;

// Static member definitions
Function *CFGMappingWitness::F = nullptr;
z3::context *CFGMappingWitness::SymCtx = nullptr;
StringBlockMap CFGMappingWitness::SrcStringMap;
StringBlockMap CFGMappingWitness::TgtStringMap;
Z3BlockMap CFGMappingWitness::SrcZ3Map;
Z3BlockMap CFGMappingWitness::TgtZ3Map;
PhiIncomingMap CFGMappingWitness::SrcPhiMap;
PhiIncomingMap CFGMappingWitness::TgtPhiMap;
RetValueMap CFGMappingWitness::SrcRetMap;
RetValueMap CFGMappingWitness::TgtRetMap;
RetPhiNameMap CFGMappingWitness::SrcRetPhiMap;
RetPhiNameMap CFGMappingWitness::TgtRetPhiMap;
PathCondMap CFGMappingWitness::SrcPathCondMap;
PathCondMap CFGMappingWitness::TgtPathCondMap;
std::map<const BasicBlock *, std::map<const Value *, std::string>>
    CFGMappingWitness::SrcBlockValues;
std::map<const BasicBlock *, std::map<const Value *, std::string>>
    CFGMappingWitness::TgtBlockValues;

// Static helper implementations
std::string CFGMappingWitness::getValueName(const Value *V) {
  if (isa<ReturnInst>(V))
    return "ret";
  if (V->hasName())
    return V->getName().str();
  std::string S;
  raw_string_ostream OS(S);
  OS << "v" << (const void *)V;
  return OS.str();
}

std::string CFGMappingWitness::escapeValueString(StringRef In) {
  std::string Out;
  Out.reserve(In.size());
  for (char C : In) {
    if (C == '\\')
      Out += "\\\\";
    else if (C == '"')
      Out += "\\\"";
    else
      Out += C;
  }
  return Out;
}

std::string CFGMappingWitness::getConstString(const ConstantInt *CI) {
  if (CI->getType()->isIntegerTy(1))
    return CI->isOne() ? "true" : "false";
  return std::to_string(CI->getSExtValue());
}

std::string CFGMappingWitness::computeInstructionExprString(
    const Instruction &I, DenseMap<const Value *, std::string> &ValMap) {
  if (auto *BO = dyn_cast<BinaryOperator>(&I)) {
    std::string L = getExprForValue(BO->getOperand(0), ValMap);
    std::string R = getExprForValue(BO->getOperand(1), ValMap);
    const char *Op = nullptr;
    switch (BO->getOpcode()) {
    case Instruction::Add:  Op = "+"; break;
    case Instruction::Sub:  Op = "-"; break;
    case Instruction::Mul:  Op = "*"; break;
    case Instruction::SDiv: Op = "/"; break;
    case Instruction::SRem: Op = "%"; break;
    case Instruction::And:  Op = "&"; break;
    case Instruction::Or:   Op = "|"; break;
    case Instruction::Xor:  Op = "^"; break;
    default: break;
    }
    if (Op)
      return "(" + L + " " + Op + " " + R + ")";
  }

  if (auto *IC = dyn_cast<ICmpInst>(&I)) {
    std::string L = getExprForValue(IC->getOperand(0), ValMap);
    std::string R = getExprForValue(IC->getOperand(1), ValMap);
    const char *Op = nullptr;
    switch (IC->getPredicate()) {
    case CmpInst::ICMP_SLT: Op = "<";  break;
    case CmpInst::ICMP_SLE: Op = "<="; break;
    case CmpInst::ICMP_SGT: Op = ">";  break;
    case CmpInst::ICMP_SGE: Op = ">="; break;
    case CmpInst::ICMP_EQ:  Op = "=="; break;
    case CmpInst::ICMP_NE:  Op = "!="; break;
    case CmpInst::ICMP_ULT: Op = "u<";  break;
    case CmpInst::ICMP_ULE: Op = "u<="; break;
    case CmpInst::ICMP_UGT: Op = "u>";  break;
    case CmpInst::ICMP_UGE: Op = "u>="; break;
    default: break;
    }
    if (Op)
      return "(" + L + " " + Op + " " + R + ")";
  }

  if (auto *PHI = dyn_cast<PHINode>(&I)) {
    if (PHI->getNumIncomingValues() > 0) {
      std::string First = getExprForValue(PHI->getIncomingValue(0), ValMap);
      bool AllSame = true;
      for (unsigned i = 1; i < PHI->getNumIncomingValues(); ++i) {
        std::string Next = getExprForValue(PHI->getIncomingValue(i), ValMap);
        if (First != Next) {
          AllSame = false;
          break;
        }
      }
      if (AllSame)
        return First;
    }

    std::string S = "phi(";
    for (unsigned i = 0; i < PHI->getNumIncomingValues(); ++i) {
      if (i)
        S += ", ";
      S += getExprForValue(PHI->getIncomingValue(i), ValMap);
    }
    S += ")";
    return S;
  }

  if (auto *SI = dyn_cast<SelectInst>(&I)) {
    std::string Cnd = getExprForValue(SI->getCondition(), ValMap);
    std::string TVal = getExprForValue(SI->getTrueValue(), ValMap);
    std::string FVal = getExprForValue(SI->getFalseValue(), ValMap);
    return "(" + Cnd + " ? " + TVal + " : " + FVal + ")";
  }

  if (auto *LI = dyn_cast<LoadInst>(&I)) {
    std::string Ptr = getExprForValue(LI->getPointerOperand(), ValMap);
    return "load(" + Ptr + ")";
  }

  return getValueName(&I);
}

std::string CFGMappingWitness::getExprForValue(
    const Value *V, DenseMap<const Value *, std::string> &ValMap) {
  auto It = ValMap.find(V);
  if (It != ValMap.end())
    return It->second;

  if (const auto *CI = dyn_cast<ConstantInt>(V))
    return getConstString(CI);

  if (isa<Argument>(V))
    return getValueName(V);

  if (auto *I = dyn_cast<Instruction>(V)) {
    // Insert the value name as a placeholder BEFORE recursing to break cycles
    // (e.g., PHI nodes with loop back-edges referencing themselves).
    std::string Name = getValueName(V);
    ValMap[V] = Name;
    std::string E = computeInstructionExprString(*I, ValMap);
    ValMap[V] = E;
    return E;
  }

  return getValueName(V);
}

// Returns true if this value's type can be reasoned about by Z3 as int/bool.
static bool isSupportedZ3Type(const Value *V) {
  Type *T = V->getType();
  return T->isIntegerTy();
}

z3::expr CFGMappingWitness::makeFreshZ3Expr(const Value *V, z3::context &C) {
  std::string Name = getValueName(V);
  if (V->getType()->isIntegerTy(1))
    return C.bool_const(Name.c_str());
  if (V->getType()->isIntegerTy())
    return C.int_const(Name.c_str());
  // For float/pointer/vector types, use uninterpreted int constant.
  return C.int_const(Name.c_str());
}

z3::expr CFGMappingWitness::computeInstructionExprZ3(
    const Instruction &I, z3::context &C,
    std::map<const Value *, z3::expr> &ValMap) {
  if (auto *BO = dyn_cast<BinaryOperator>(&I)) {
    // Only handle integer binary ops
    if (!BO->getType()->isIntegerTy())
      return makeFreshZ3Expr(&I, C);
    z3::expr L = z3GetExprForValue(BO->getOperand(0), C, ValMap);
    z3::expr R = z3GetExprForValue(BO->getOperand(1), C, ValMap);
    switch (BO->getOpcode()) {
    case Instruction::Add:  return L + R;
    case Instruction::Sub:  return L - R;
    case Instruction::Mul:  return L * R;
    case Instruction::SDiv: return L / R;
    case Instruction::SRem: return L % R;
    case Instruction::And:
      if (BO->getType()->isIntegerTy(1))
        return L && R;
      // Bitwise AND on multi-bit integers is not expressible in Z3 LIA
      return makeFreshZ3Expr(&I, C);
    case Instruction::Or:
      if (BO->getType()->isIntegerTy(1))
        return L || R;
      // Bitwise OR on multi-bit integers is not expressible in Z3 LIA
      return makeFreshZ3Expr(&I, C);
    case Instruction::Xor:
      if (BO->getType()->isIntegerTy(1))
        return !(L == R); // XOR as not-equal for booleans
      // Bitwise XOR on multi-bit integers is not expressible in Z3 LIA
      return makeFreshZ3Expr(&I, C);
    default:
      break;
    }
  }

  if (auto *IC = dyn_cast<ICmpInst>(&I)) {
    z3::expr L = z3GetExprForValue(IC->getOperand(0), C, ValMap);
    z3::expr R = z3GetExprForValue(IC->getOperand(1), C, ValMap);
    // Guard: sorts must match for any comparison to avoid Z3 assertion failures
    if (L.get_sort().id() != R.get_sort().id())
      return makeFreshZ3Expr(&I, C);
    switch (IC->getPredicate()) {
    case CmpInst::ICMP_EQ:  return L == R;
    case CmpInst::ICMP_NE:  return L != R;
    // Signed integer comparisons (only valid for int sort, not bool)
    case CmpInst::ICMP_SLT:
      if (!L.is_bool()) return L < R;
      return makeFreshZ3Expr(&I, C);
    case CmpInst::ICMP_SLE:
      if (!L.is_bool()) return L <= R;
      return makeFreshZ3Expr(&I, C);
    case CmpInst::ICMP_SGT:
      if (!L.is_bool()) return L > R;
      return makeFreshZ3Expr(&I, C);
    case CmpInst::ICMP_SGE:
      if (!L.is_bool()) return L >= R;
      return makeFreshZ3Expr(&I, C);
    // Unsigned comparisons: z3::ult/ugt/ule/uge require bitvector sort but we
    // use integer sort, so we cannot express these. Return an opaque fresh var.
    case CmpInst::ICMP_ULT:
    case CmpInst::ICMP_ULE:
    case CmpInst::ICMP_UGT:
    case CmpInst::ICMP_UGE:
      return makeFreshZ3Expr(&I, C);
    default:
      break;
    }
  }

  if (auto *PHI = dyn_cast<PHINode>(&I)) {
    (void)PHI;
    // Represent PHI as a symbolic variable; incoming resolution handled via
    // PhiMap.
    return makeFreshZ3Expr(&I, C);
  }

  if (auto *SI = dyn_cast<SelectInst>(&I)) {
    // Use ITE to correctly model select
    if (!isSupportedZ3Type(SI->getTrueValue()) ||
        !isSupportedZ3Type(SI->getFalseValue()))
      return makeFreshZ3Expr(&I, C);
    z3::expr Cond = z3GetExprForValue(SI->getCondition(), C, ValMap);
    z3::expr TVal = z3GetExprForValue(SI->getTrueValue(), C, ValMap);
    z3::expr FVal = z3GetExprForValue(SI->getFalseValue(), C, ValMap);
    // Ensure Cond is bool
    if (!Cond.is_bool())
      Cond = (Cond != C.int_val(0));
    return z3::ite(Cond, TVal, FVal);
  }

  if (auto *LI = dyn_cast<LoadInst>(&I)) {
    (void)LI;
    return makeFreshZ3Expr(&I, C);
  }

  return makeFreshZ3Expr(&I, C);
}

z3::expr CFGMappingWitness::z3GetExprForValue(
    const Value *V, z3::context &C, std::map<const Value *, z3::expr> &ValMap) {
  auto It = ValMap.find(V);
  if (It != ValMap.end())
    return It->second;

  if (const auto *RI = dyn_cast<ReturnInst>(V)) {
    if (Value *RV = RI->getReturnValue())
      return z3GetExprForValue(RV, C, ValMap);
    return C.bool_val(true);
  }

  if (const auto *CI = dyn_cast<ConstantInt>(V)) {
    if (CI->getType()->isIntegerTy(1))
      return C.bool_val(CI->isOne());
    return C.int_val(CI->getSExtValue());
  }

  if (isa<Argument>(V)) {
    z3::expr E = makeFreshZ3Expr(V, C);
    ValMap.insert({V, E});
    return E;
  }

  if (auto *I = dyn_cast<Instruction>(V)) {
    // Pre-insert a placeholder (fresh var) to break potential cycles.
    z3::expr Placeholder = makeFreshZ3Expr(V, C);
    ValMap.insert({V, Placeholder});
    z3::expr E = computeInstructionExprZ3(*I, C, ValMap);
    // Update the map entry with the actual expression.
    ValMap.insert_or_assign(V, E);
    return E;
  }

  z3::expr Fallback = makeFreshZ3Expr(V, C);
  ValMap.insert({V, Fallback});
  return Fallback;
}

void CFGMappingWitness::initialize(Function *Func, z3::context *C) {
  F = Func;
  SymCtx = C;
  clear();
}

void CFGMappingWitness::clear() {
  SrcStringMap.clear();
  TgtStringMap.clear();
  SrcZ3Map.clear();
  TgtZ3Map.clear();
  SrcPhiMap.clear();
  TgtPhiMap.clear();
  SrcRetMap.clear();
  TgtRetMap.clear();
  SrcRetPhiMap.clear();
  TgtRetPhiMap.clear();
  SrcPathCondMap.clear();
  TgtPathCondMap.clear();
  SrcBlockValues.clear();
  TgtBlockValues.clear();
}

z3::context &CFGMappingWitness::getContext() {
  assert(SymCtx && "CFGMappingWitness::getContext called before initialize");
  return *SymCtx;
}

void CFGMappingWitness::buildSourceMappings() {
  errs() << "\n*** CFGMappingWitness::buildSourceMappings() ***\n";

  SrcStringMap = buildBlockStringValueMap(/*isSrc=*/true);
  SrcZ3Map = buildBlockZ3ValueMap(&SrcPhiMap, &SrcRetMap, &SrcRetPhiMap);
  SrcPathCondMap = buildPathCondMap(SrcZ3Map);

  dumpSourceStringMappings("Pre-Simplify Source String Mappings");
  dumpSourceZ3Mappings("Pre-Simplify Source Z3 Mappings");
  dumpSourceZ3MapEntries("Pre-Simplify Source Z3 Map Entries");
  dumpSourcePhiMappings("Pre-Simplify Source PHI Mappings");
}

void CFGMappingWitness::buildTargetMappings() {
  errs() << "\n*** CFGMappingWitness::buildTargetMappings() ***\n";

  TgtStringMap = buildBlockStringValueMap(/*isSrc=*/false);
  TgtZ3Map = buildBlockZ3ValueMap(&TgtPhiMap, &TgtRetMap, &TgtRetPhiMap);
  TgtPathCondMap = buildPathCondMap(TgtZ3Map);

  dumpTargetStringMappings("Post-Simplify Target String Mappings");
  dumpTargetZ3Mappings("Post-Simplify Target Z3 Mappings");
  dumpTargetZ3MapEntries("Post-Simplify Target Z3 Map Entries");
  dumpTargetPhiMappings("Post-Simplify Target PHI Mappings");
}

// Returns true if the variable name should be included in verification maps.
// We skip unnamed (v0x...) values and void-type instructions.
static bool shouldIncludeVar(const std::string &Name, const Value *V) {
  if (Name.rfind("v0x", 0) == 0)
    return false;
  if (isa<AllocaInst>(V))
    return false;
  if (isa<Constant>(V) && !isa<ConstantInt>(V))
    return false;
  return true;
}

StringBlockMap CFGMappingWitness::buildBlockStringValueMap(bool isSrc) {
  StringBlockMap Result;

  for (BasicBlock &BB : *F) {
    // Pointer-keyed map for SrcBlockValues (debug only)
    std::map<const Value *, std::string> BlockPtrMap;
    // String-keyed map for the result
    std::map<std::string, std::string> BlockStrMap;

    DenseMap<const Value *, std::string> ValMap;

    for (Instruction &I : BB) {
      if (I.getType()->isVoidTy())
        continue;
      std::string Expr = getExprForValue(&I, ValMap);
      std::string VarName = getValueName(&I);
      BlockPtrMap[&I] = Expr;
      if (shouldIncludeVar(VarName, &I))
        BlockStrMap[VarName] = Expr;
    }

    // Handle return value separately via RetMap — don't add to block maps
    // (avoids confusion with "ret" key during accumulation).

    std::string BBlockName =
        BB.hasName() ? BB.getName().str() : std::string("(unnamed)");
    Result[BBlockName] = BlockStrMap;

    if (isSrc)
      SrcBlockValues[&BB] = BlockPtrMap;
    else
      TgtBlockValues[&BB] = BlockPtrMap;
  }

  return Result;
}

Z3BlockMap CFGMappingWitness::buildBlockZ3ValueMap(PhiIncomingMap *PhiMap,
                                                   RetValueMap *RetMap,
                                                   RetPhiNameMap *RetPhiMap) {
  Z3BlockMap Result;
  // Internal computation map: Value* -> z3::expr (alive during build only)
  std::map<const Value *, z3::expr> GlobalMap;

  // Add function arguments to global map
  for (Argument &A : F->args())
    GlobalMap.insert({&A, makeFreshZ3Expr(&A, *SymCtx)});

  // Pre-pass: for each AllocaInst, if every store to it writes the same
  // constant, record that constant so loads can be resolved concretely.
  std::map<const Value *, z3::expr> ConstAllocaMap;
  for (BasicBlock &BB : *F) {
    for (Instruction &I : BB) {
      if (auto *AI = dyn_cast<AllocaInst>(&I)) {
        std::optional<int64_t> ConstVal;
        bool AllSameConst = true;
        for (User *U : AI->users()) {
          if (auto *SI = dyn_cast<StoreInst>(U)) {
            if (auto *CI = dyn_cast<ConstantInt>(SI->getValueOperand())) {
              int64_t V = CI->getSExtValue();
              if (!ConstVal.has_value())
                ConstVal = V;
              else if (*ConstVal != V) {
                AllSameConst = false;
                break;
              }
            } else {
              AllSameConst = false;
              break;
            }
          }
        }
        if (AllSameConst && ConstVal.has_value()) {
          errs() << "  [ConstAlloca] " << AI->getName()
                 << " always stores " << *ConstVal << "\n";
          ConstAllocaMap.insert_or_assign(AI, SymCtx->int_val(*ConstVal));
        }
      }
    }
  }

  for (BasicBlock &BB : *F) {
    std::string BlockName =
        BB.hasName() ? BB.getName().str() : std::string("(unnamed)");
    std::map<const Value *, z3::expr> LocalMap = GlobalMap;

    // String-keyed output map (no dangling pointers after transformation)
    std::map<std::string, z3::expr> StrMap;

    for (Instruction &I : BB) {
      // Resolve loads from constant-store allocas concretely.
      if (auto *LI = dyn_cast<LoadInst>(&I)) {
        Value *Ptr = LI->getPointerOperand();
        auto It = ConstAllocaMap.find(Ptr);
        if (It != ConstAllocaMap.end()) {
          LocalMap.insert({&I, It->second});
          std::string VarName = getValueName(&I);
          if (shouldIncludeVar(VarName, &I))
            StrMap.insert_or_assign(VarName, It->second);
          continue;
        }
      }

      if (I.getType()->isVoidTy())
        continue;

      z3::expr E = computeInstructionExprZ3(I, *SymCtx, LocalMap);
      LocalMap.insert({&I, E});

      std::string VarName = getValueName(&I);
      if (shouldIncludeVar(VarName, &I))
        StrMap.insert_or_assign(VarName, E);

      if (auto *PHI = dyn_cast<PHINode>(&I)) {
        if (PhiMap) {
          std::string PhiName = getValueName(PHI);
          auto &IncomingMap = (*PhiMap)[PhiName];
          for (unsigned i = 0; i < PHI->getNumIncomingValues(); ++i) {
            BasicBlock *InBB = PHI->getIncomingBlock(i);
            std::string InName = InBB && InBB->hasName()
                                     ? InBB->getName().str()
                                     : std::string("(unnamed)");
            z3::expr InVal =
                z3GetExprForValue(PHI->getIncomingValue(i), *SymCtx, LocalMap);
            IncomingMap.insert_or_assign(InName, InVal);
          }
        }
      }
    }

    if (auto *RI = dyn_cast<ReturnInst>(BB.getTerminator())) {
      if (Value *RV = RI->getReturnValue()) {
        z3::expr RetExpr = z3GetExprForValue(RV, *SymCtx, LocalMap);
        errs() << "ret_expr_=" << Z3_ast_to_string(*SymCtx, RetExpr);
        errs() << "RI: " << *RI << "\n";
        if (RetMap)
          RetMap->insert_or_assign(BlockName, RetExpr);
        if (RetPhiMap) {
          if (isa<PHINode>(RV))
            RetPhiMap->insert_or_assign(BlockName, getValueName(RV));
        }
      }
    }

    Result[BlockName] = std::move(StrMap);
  }

  return Result;
}

PathCondMap CFGMappingWitness::buildPathCondMap(const Z3BlockMap &Z3Map) {
  PathCondMap Result;
  z3::context &C = getContext();

  auto get_block_name = [](const BasicBlock &BB) {
    return BB.hasName() ? BB.getName().str() : std::string("(unnamed)");
  };

  for (const BasicBlock &BB : *F)
    Result.emplace(get_block_name(BB), C.bool_val(false));

  BasicBlock &Entry = F->getEntryBlock();
  {
    std::string EntryName = get_block_name(Entry);
    auto It = Result.find(EntryName);
    if (It != Result.end())
      It->second = C.bool_val(true);
    else
      Result.emplace(EntryName, C.bool_val(true));
  }

  SmallVector<BasicBlock *, 16> Worklist;
  Worklist.push_back(&Entry);

  // Lookup a value's Z3 expression from the string-keyed Z3Map.
  // V must be from the live IR (its name is stable).
  auto get_value_expr = [&](const Value *V, const BasicBlock &BB) -> z3::expr {
    auto BlockIt = Z3Map.find(get_block_name(BB));
    if (BlockIt != Z3Map.end()) {
      std::string VName = getValueName(V);
      auto ValIt = BlockIt->second.find(VName);
      if (ValIt != BlockIt->second.end())
        return ValIt->second;
    }
    // Fallback: create a fresh symbolic variable
    return makeFreshZ3Expr(V, C);
  };

  auto update_successor = [&](BasicBlock *Succ, const z3::expr &Cond) {
    std::string Name = get_block_name(*Succ);
    auto It = Result.find(Name);
    if (It == Result.end())
      It = Result.emplace(Name, C.bool_val(false)).first;
    z3::expr Current = It->second;
    z3::expr Combined = Current || Cond;
    if (!z3::eq(Combined, Current)) {
      It->second = Combined;
      Worklist.push_back(Succ);
    }
  };

  SmallPtrSet<BasicBlock *, 32> Visited;
  while (!Worklist.empty()) {
    BasicBlock *BB = Worklist.pop_back_val();
    // Prevent infinite loops in cyclic CFGs
    if (!Visited.insert(BB).second)
      continue;

    std::string BBName = get_block_name(*BB);
    auto It = Result.find(BBName);
    if (It == Result.end())
      It = Result.emplace(BBName, C.bool_val(false)).first;
    z3::expr BBCond = It->second;

    if (auto *BI = dyn_cast<BranchInst>(BB->getTerminator())) {
      if (BI->isUnconditional()) {
        update_successor(BI->getSuccessor(0), BBCond);
        continue;
      }

      z3::expr CondExpr = get_value_expr(BI->getCondition(), *BB);
      // Safely convert to bool; branch conditions are always i1
      if (!CondExpr.is_bool()) {
        if (CondExpr.is_int())
          CondExpr = (CondExpr != C.int_val(0));
        else
          CondExpr = C.bool_const(("cond_" + std::to_string(
              reinterpret_cast<uintptr_t>(BI))).c_str());
      }
      update_successor(BI->getSuccessor(0), BBCond && CondExpr);
      update_successor(BI->getSuccessor(1), BBCond && !CondExpr);
      continue;
    }

    if (auto *SI = dyn_cast<SwitchInst>(BB->getTerminator())) {
      z3::expr CondExpr = get_value_expr(SI->getCondition(), *BB);
      z3::expr DefaultCond = BBCond;
      std::map<const Value *, z3::expr> EmptyMap;
      for (auto &Case : SI->cases()) {
        const ConstantInt *CaseVal = Case.getCaseValue();
        z3::expr CaseExpr = z3GetExprForValue(CaseVal, C, EmptyMap);
        // Guard against sort mismatches between switch condition and case value
        z3::expr Match = (CondExpr.get_sort().id() == CaseExpr.get_sort().id())
                             ? (CondExpr == CaseExpr)
                             : C.bool_val(false);
        update_successor(Case.getCaseSuccessor(), BBCond && Match);
        DefaultCond = DefaultCond && !Match;
      }
      update_successor(SI->getDefaultDest(), DefaultCond);
      continue;
    }
  }

  return Result;
}

void CFGMappingWitness::dumpSourceStringMappings(const char *Label) {
  errs() << "\n=== " << Label << " ===\n";
  for (BasicBlock &BB : *F) {
    std::string BlockName =
        BB.hasName() ? BB.getName().str() : std::string("(unnamed)");
    errs() << "  {" << BlockName << ": {";
    auto It = SrcStringMap.find(BlockName);
    bool First = true;
    if (It != SrcStringMap.end()) {
      for (const auto &KV : It->second) {
        if (!First)
          errs() << ", ";
        // KV.first is now std::string (the variable name)
        errs() << "\"" << KV.first << "\": \""
               << escapeValueString(KV.second) << "\"";
        First = false;
      }
    }
    errs() << "}}\n";
  }
}

void CFGMappingWitness::dumpSourceZ3Mappings(const char *Label) {
  errs() << "\n=== " << Label << " ===\n";
  for (BasicBlock &BB : *F) {
    std::string BlockName =
        BB.hasName() ? BB.getName().str() : std::string("(unnamed)");
    errs() << "  {" << BlockName << ": {";
    auto It = SrcZ3Map.find(BlockName);
    bool First = true;
    if (It != SrcZ3Map.end()) {
      for (const auto &KV : It->second) {
        if (!First)
          errs() << ", ";
        errs() << "\"" << KV.first << "\": \""
               << escapeValueString(Z3_ast_to_string(*SymCtx, KV.second))
               << "\"";
        First = false;
      }
    }
    errs() << "}}\n";
  }
}

void CFGMappingWitness::dumpSourceZ3MapEntries(const char *Label) {
  errs() << "\n=== " << Label << " ===\n";
  for (const auto &BlockEntry : SrcZ3Map) {
    errs() << "  {" << BlockEntry.first << ": {";
    bool First = true;
    for (const auto &KV : BlockEntry.second) {
      if (!First)
        errs() << ", ";
      errs() << "\"" << KV.first << "\": \""
             << escapeValueString(Z3_ast_to_string(*SymCtx, KV.second)) << "\"";
      First = false;
    }
    errs() << "}}\n";
  }
}

void CFGMappingWitness::dumpSourcePhiMappings(const char *Label) {
  errs() << "\n=== " << Label << " ===\n";
  for (const auto &PhiEntry : SrcPhiMap) {
    errs() << "  {\"" << PhiEntry.first << "\": {";
    bool First = true;
    for (const auto &Incoming : PhiEntry.second) {
      if (!First)
        errs() << ", ";
      errs() << "\"" << Incoming.first << "\": \""
             << escapeValueString(Z3_ast_to_string(*SymCtx, Incoming.second))
             << "\"";
      First = false;
    }
    errs() << "}}\n";
  }
}

void CFGMappingWitness::dumpTargetStringMappings(const char *Label) {
  errs() << "\n=== " << Label << " ===\n";
  for (BasicBlock &BB : *F) {
    std::string BlockName =
        BB.hasName() ? BB.getName().str() : std::string("(unnamed)");
    errs() << "  {" << BlockName << ": {";
    auto It = TgtStringMap.find(BlockName);
    bool First = true;
    if (It != TgtStringMap.end()) {
      for (const auto &KV : It->second) {
        if (!First)
          errs() << ", ";
        errs() << "\"" << KV.first << "\": \""
               << escapeValueString(KV.second) << "\"";
        First = false;
      }
    }
    errs() << "}}\n";
  }
}

void CFGMappingWitness::dumpTargetZ3Mappings(const char *Label) {
  errs() << "\n=== " << Label << " ===\n";
  for (BasicBlock &BB : *F) {
    std::string BlockName =
        BB.hasName() ? BB.getName().str() : std::string("(unnamed)");
    errs() << "  {" << BlockName << ": {";
    auto It = TgtZ3Map.find(BlockName);
    bool First = true;
    if (It != TgtZ3Map.end()) {
      for (const auto &KV : It->second) {
        if (!First)
          errs() << ", ";
        errs() << "\"" << KV.first << "\": \""
               << escapeValueString(Z3_ast_to_string(*SymCtx, KV.second))
               << "\"";
        First = false;
      }
    }
    errs() << "}}\n";
  }
}

void CFGMappingWitness::dumpTargetZ3MapEntries(const char *Label) {
  errs() << "\n=== " << Label << " ===\n";
  for (const auto &BlockEntry : TgtZ3Map) {
    errs() << "  {" << BlockEntry.first << ": {";
    bool First = true;
    for (const auto &KV : BlockEntry.second) {
      if (!First)
        errs() << ", ";
      errs() << "\"" << KV.first << "\": \""
             << escapeValueString(Z3_ast_to_string(*SymCtx, KV.second)) << "\"";
      First = false;
    }
    errs() << "}}\n";
  }
}

void CFGMappingWitness::dumpTargetPhiMappings(const char *Label) {
  errs() << "\n=== " << Label << " ===\n";
  for (const auto &PhiEntry : TgtPhiMap) {
    errs() << "  {\"" << PhiEntry.first << "\": {";
    bool First = true;
    for (const auto &Incoming : PhiEntry.second) {
      if (!First)
        errs() << ", ";
      errs() << "\"" << Incoming.first << "\": \""
             << escapeValueString(Z3_ast_to_string(*SymCtx, Incoming.second))
             << "\"";
      First = false;
    }
    errs() << "}}\n";
  }
}

const std::map<const Value *, std::string> &
CFGMappingWitness::getSourceBlockValues(const BasicBlock *BB) {
  auto It = SrcBlockValues.find(BB);
  if (It != SrcBlockValues.end())
    return It->second;
  static const std::map<const Value *, std::string> Empty;
  return Empty;
}

const std::map<const Value *, std::string> &
CFGMappingWitness::getTargetBlockValues(const BasicBlock *BB) {
  auto It = TgtBlockValues.find(BB);
  if (It != TgtBlockValues.end())
    return It->second;
  static const std::map<const Value *, std::string> Empty;
  return Empty;
}

/// Build implication: (src_state && tgt_state && param_eq) ->
/// (common_variable_equalities)
/// Now uses string-keyed maps — no dangling Value* access after transformation.
z3::expr
CFGMappingWitness::buildCommonVariableEquality(z3::context &C,
                                               ArrayRef<std::string> src_blocks,
                                               ArrayRef<std::string> tgt_blocks,
                                               ArrayRef<std::string> tgt_phi_hint) {
  std::map<std::string, z3::expr> src_values;
  std::map<std::string, z3::expr> tgt_values;

  // Accumulate variable->z3 mappings from a set of blocks.
  // Uses the string-keyed SrcStringMap/TgtStringMap to enumerate variables,
  // then looks up z3 expressions from the string-keyed SrcZ3Map/TgtZ3Map.
  // This avoids any access to freed Value* pointers after transformation.
  auto accumulate_values =
      [](const StringBlockMap &StrMap, const Z3BlockMap &Z3Map,
         const PhiIncomingMap &PhiMap, ArrayRef<std::string> Blocks,
         ArrayRef<std::string> PhiHint,
         std::map<std::string, z3::expr> &Out) {
        // If PhiHint is non-empty, use it for PHI incoming block resolution.
        ArrayRef<std::string> PhiBlocks = PhiHint.empty() ? Blocks : PhiHint;

        for (const std::string &Block : Blocks) {
          auto StrIt = StrMap.find(Block);
          if (StrIt == StrMap.end())
            continue;
          auto Z3It = Z3Map.find(Block);
          if (Z3It == Z3Map.end())
            continue;

          for (const auto &KV : StrIt->second) {
            // KV.first is now std::string (the variable name) — safe to use!
            const std::string &VarName = KV.first;

            if (Out.count(VarName) != 0)
              continue;

            // Check if this is a PHI node (it appears in PhiMap)
            auto PhiIt = PhiMap.find(VarName);
            if (PhiIt != PhiMap.end()) {
              // Resolve PHI using the appropriate incoming block
              for (StringRef IncomingBlock : PhiBlocks) {
                auto IncomingIt = PhiIt->second.find(IncomingBlock.str());
                if (IncomingIt != PhiIt->second.end()) {
                  Out.emplace(VarName, IncomingIt->second);
                  break;
                }
              }
              if (Out.count(VarName) != 0)
                continue;
            }

            // Look up the Z3 expression by variable name
            auto ExprIt = Z3It->second.find(VarName);
            if (ExprIt == Z3It->second.end())
              continue;

            Out.emplace(VarName, ExprIt->second);
          }
        }
      };

  auto add_return_value = [&](const RetValueMap &RetMap,
                              ArrayRef<std::string> Blocks,
                              std::map<std::string, z3::expr> &Out) {
    if (Out.count("ret") != 0)
      return;
    for (const std::string &Block : Blocks) {
      auto It = RetMap.find(Block);
      if (It != RetMap.end()) {
        Out.emplace("ret", It->second);
        return;
      }
    }
  };

  accumulate_values(SrcStringMap, SrcZ3Map, SrcPhiMap, src_blocks, {}, src_values);
  accumulate_values(TgtStringMap, TgtZ3Map, TgtPhiMap, tgt_blocks, tgt_phi_hint, tgt_values);
  add_return_value(SrcRetMap, src_blocks, src_values);
  add_return_value(TgtRetMap, tgt_blocks, tgt_values);

  z3::expr_vector src_state_conj(C);
  z3::expr_vector tgt_state_conj(C);
  std::map<std::string, z3::expr> src_renamed_vars;
  std::map<std::string, z3::expr> tgt_renamed_vars;

  auto make_same_sort_var = [&](const std::string &name, const z3::expr &val) {
    if (val.is_bool())
      return C.bool_const(name.c_str());
    if (val.is_int())
      return C.int_const(name.c_str());
    return C.int_const(name.c_str());
  };

  for (Argument &A : F->args()) {
    std::string Name = getValueName(&A);
    z3::expr base = makeFreshZ3Expr(&A, *SymCtx);
    src_renamed_vars.insert({Name, make_same_sort_var(Name + "_src", base)});
    tgt_renamed_vars.insert({Name, make_same_sort_var(Name + "_tgt", base)});
  }

  for (const auto &[name, val] : src_values) {
    std::string renamed_name = name + "_src";
    z3::expr renamed_var = make_same_sort_var(renamed_name, val);
    src_renamed_vars.insert({name, renamed_var});
  }

  for (const auto &[name, val] : tgt_values) {
    std::string renamed_name = name + "_tgt";
    z3::expr renamed_var = make_same_sort_var(renamed_name, val);
    tgt_renamed_vars.insert({name, renamed_var});
  }

  auto build_substitution = [&](const std::map<std::string, z3::expr> &values,
                                const std::map<std::string, z3::expr> &renamed,
                                z3::expr_vector &from, z3::expr_vector &to) {
    for (const auto &[name, val] : values) {
      auto it = renamed.find(name);
      if (it == renamed.end())
        continue;
      from.push_back(make_same_sort_var(name, val));
      to.push_back(it->second);
    }
  };

  z3::expr_vector src_from(C);
  z3::expr_vector src_to(C);
  z3::expr_vector tgt_from(C);
  z3::expr_vector tgt_to(C);
  build_substitution(src_values, src_renamed_vars, src_from, src_to);
  build_substitution(tgt_values, tgt_renamed_vars, tgt_from, tgt_to);

  for (const auto &[name, val] : src_values) {
    const z3::expr &renamed_var = src_renamed_vars.at(name);
    z3::expr renamed_val = z3::expr(val).substitute(src_from, src_to);
    if (!z3::eq(renamed_var, renamed_val))
      src_state_conj.push_back(renamed_var == renamed_val);
  }

  for (const auto &[name, val] : tgt_values) {
    const z3::expr &renamed_var = tgt_renamed_vars.at(name);
    z3::expr renamed_val = z3::expr(val).substitute(tgt_from, tgt_to);
    if (!z3::eq(renamed_var, renamed_val))
      tgt_state_conj.push_back(renamed_var == renamed_val);
  }

  z3::expr_vector param_equalities(C);
  for (Argument &A : F->args()) {
    std::string Name = getValueName(&A);
    auto src_it = src_renamed_vars.find(Name);
    auto tgt_it = tgt_renamed_vars.find(Name);
    if (src_it != src_renamed_vars.end() && tgt_it != tgt_renamed_vars.end()) {
      const z3::expr &src_param = src_it->second;
      const z3::expr &tgt_param = tgt_it->second;
      if (!z3::eq(src_param, tgt_param))
        param_equalities.push_back(src_param == tgt_param);
    }
  }

  z3::expr premise = C.bool_val(true);
  if (!src_state_conj.empty())
    premise = premise && z3::mk_and(src_state_conj);
  if (!tgt_state_conj.empty())
    premise = premise && z3::mk_and(tgt_state_conj);
  if (!param_equalities.empty())
    premise = premise && z3::mk_and(param_equalities);

  std::set<std::string> param_names;
  for (const Argument &A : F->args())
    param_names.insert(getValueName(&A));

  // Classify intermediate variable equalities into:
  //   (a) trivial-trivial: both opaque/fresh PHI vars → move to premise as
  //       shared assumptions (they represent the same unchanged SSA value)
  //   (b) non-trivial-non-trivial: both expressible → put in conclusion
  //   (c) mixed (one trivial, one non-trivial): skip (unprovable)
  auto is_non_trivial_expr = [](const z3::expr &e) -> bool {
    return e.num_args() > 0;
  };

  z3::expr_vector conclusion_equalities(C);

  for (const auto &[name, src_renamed] : src_renamed_vars) {
    if (param_names.count(name) != 0)
      continue;
    if (name == "ret")
      continue;
    auto it = tgt_renamed_vars.find(name);
    if (it == tgt_renamed_vars.end())
      continue;
    const z3::expr &tgt_renamed = it->second;
    if (src_renamed.get_sort().id() != tgt_renamed.get_sort().id())
      continue;
    auto src_val_it = src_values.find(name);
    auto tgt_val_it = tgt_values.find(name);
    bool src_non_trivial = src_val_it != src_values.end() &&
                           is_non_trivial_expr(src_val_it->second);
    bool tgt_non_trivial = tgt_val_it != tgt_values.end() &&
                           is_non_trivial_expr(tgt_val_it->second);
    if (!src_non_trivial && !tgt_non_trivial) {
      // Both opaque (unchanged PHI/fresh vars): add to premise as a shared
      // assumption — they represent the same unchanged SSA value.
      premise = premise && (src_renamed == tgt_renamed);
    } else if (src_non_trivial && tgt_non_trivial) {
      // Both expressible: verify equality in the conclusion.
      conclusion_equalities.push_back(src_renamed == tgt_renamed);
    } else {
      // Mixed (one opaque PHI, one expressible select/ite): the witness mapping
      // itself asserts the equivalence (e.g., PHI eliminated to select).
      // Add to premise as a witness-backed assumption so derived computations
      // can be checked downstream.
      premise = premise && (src_renamed == tgt_renamed);
    }
  }

  // Return value equality is the primary semantic check.
  auto ret_src_it = src_renamed_vars.find("ret");
  auto ret_tgt_it = tgt_renamed_vars.find("ret");
  if (ret_src_it != src_renamed_vars.end() &&
      ret_tgt_it != tgt_renamed_vars.end()) {
    const z3::expr &ret_src = ret_src_it->second;
    const z3::expr &ret_tgt = ret_tgt_it->second;
    if (ret_src.get_sort().id() == ret_tgt.get_sort().id()) {
      conclusion_equalities.push_back(ret_src == ret_tgt);
    } else {
      errs() << "[ret] sort mismatch, skipping ret equality\n";
    }
  } else if ((ret_src_it == src_renamed_vars.end()) !=
             (ret_tgt_it == tgt_renamed_vars.end())) {
    errs() << "[ret] missing in "
           << (ret_src_it == src_renamed_vars.end() ? "src" : "tgt")
           << " values — skipping ret equality\n";
  }

  z3::expr conclusion = C.bool_val(true);
  if (!conclusion_equalities.empty())
    conclusion = z3::mk_and(conclusion_equalities);

  return z3::implies(premise, conclusion);
}
