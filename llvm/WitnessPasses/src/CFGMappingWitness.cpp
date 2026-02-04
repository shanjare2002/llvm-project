#include "CFGMappingWitness.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Support/raw_ostream.h"

#include <map>
#include <string>

using namespace llvm;

// Static member definitions
Function *CFGMappingWitness::F = nullptr;
z3::context *CFGMappingWitness::SymCtx = nullptr;
StringBlockMap CFGMappingWitness::SrcStringMap;
StringBlockMap CFGMappingWitness::TgtStringMap;
Z3BlockMap CFGMappingWitness::SrcZ3Map;
Z3BlockMap CFGMappingWitness::TgtZ3Map;
std::map<const BasicBlock *, std::map<const Value *, std::string>>
    CFGMappingWitness::SrcBlockValues;
std::map<const BasicBlock *, std::map<const Value *, std::string>>
    CFGMappingWitness::TgtBlockValues;

// Static helper implementations
std::string CFGMappingWitness::getValueName(const Value *V) {
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
    case Instruction::Add:
      Op = "+";
      break;
    case Instruction::Sub:
      Op = "-";
      break;
    case Instruction::Mul:
      Op = "*";
      break;
    case Instruction::And:
      Op = "&";
      break;
    case Instruction::Or:
      Op = "|";
      break;
    default:
      break;
    }
    if (Op)
      return "(" + L + " " + Op + " " + R + ")";
  }

  if (auto *IC = dyn_cast<ICmpInst>(&I)) {
    std::string L = getExprForValue(IC->getOperand(0), ValMap);
    std::string R = getExprForValue(IC->getOperand(1), ValMap);
    const char *Op = nullptr;
    switch (IC->getPredicate()) {
    case CmpInst::ICMP_SLT:
      Op = "<";
      break;
    case CmpInst::ICMP_SLE:
      Op = "<=";
      break;
    case CmpInst::ICMP_SGT:
      Op = ">";
      break;
    case CmpInst::ICMP_SGE:
      Op = ">=";
      break;
    case CmpInst::ICMP_EQ:
      Op = "==";
      break;
    case CmpInst::ICMP_NE:
      Op = "!=";
      break;
    default:
      break;
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
    std::string TVal = getExprForValue(SI->getTrueValue(), ValMap);
    std::string FVal = getExprForValue(SI->getFalseValue(), ValMap);
    return "(" + TVal + " || " + FVal + ")";
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
    std::string E = computeInstructionExprString(*I, ValMap);
    ValMap[V] = E;
    return E;
  }

  return getValueName(V);
}

z3::expr CFGMappingWitness::makeFreshZ3Expr(const Value *V, z3::context &C) {
  std::string Name = getValueName(V);
  if (V->getType()->isIntegerTy(1))
    return C.bool_const(Name.c_str());
  if (V->getType()->isIntegerTy())
    return C.int_const(Name.c_str());
  return C.int_const(Name.c_str());
}

z3::expr CFGMappingWitness::computeInstructionExprZ3(
    const Instruction &I, z3::context &C,
    std::map<const Value *, z3::expr> &ValMap) {
  if (auto *BO = dyn_cast<BinaryOperator>(&I)) {
    z3::expr L = z3GetExprForValue(BO->getOperand(0), C, ValMap);
    z3::expr R = z3GetExprForValue(BO->getOperand(1), C, ValMap);
    switch (BO->getOpcode()) {
    case Instruction::Add:
      return L + R;
    case Instruction::Sub:
      return L - R;
    case Instruction::Mul:
      return L * R;
    case Instruction::And:
      if (BO->getType()->isIntegerTy(1))
        return L && R;
      return L & R;
    case Instruction::Or:
      if (BO->getType()->isIntegerTy(1))
        return L || R;
      return L | R;
    default:
      break;
    }
  }

  if (auto *IC = dyn_cast<ICmpInst>(&I)) {
    z3::expr L = z3GetExprForValue(IC->getOperand(0), C, ValMap);
    z3::expr R = z3GetExprForValue(IC->getOperand(1), C, ValMap);
    switch (IC->getPredicate()) {
    case CmpInst::ICMP_SLT:
      return L < R;
    case CmpInst::ICMP_SLE:
      return L <= R;
    case CmpInst::ICMP_SGT:
      return L > R;
    case CmpInst::ICMP_SGE:
      return L >= R;
    case CmpInst::ICMP_EQ:
      return L == R;
    case CmpInst::ICMP_NE:
      return L != R;
    default:
      break;
    }
  }

  if (auto *PHI = dyn_cast<PHINode>(&I)) {
    if (PHI->getNumIncomingValues() > 0) {
      // Check if all incoming values are identical
      z3::expr First = z3GetExprForValue(PHI->getIncomingValue(0), C, ValMap);
      bool AllSame = true;
      for (unsigned i = 1; i < PHI->getNumIncomingValues(); ++i) {
        z3::expr Next = z3GetExprForValue(PHI->getIncomingValue(i), C, ValMap);
        if (!z3::eq(First, Next)) {
          AllSame = false;
          break;
        }
      }
      if (AllSame)
        return First;

      // If not all same, create a disjunction: result could be any incoming
      // value PHI result = incoming[0] OR result = incoming[1] OR ...
      z3::expr ResultVar = makeFreshZ3Expr(&I, C);
      z3::expr PhiExpr = (ResultVar == First);
      for (unsigned i = 1; i < PHI->getNumIncomingValues(); ++i) {
        z3::expr Incoming =
            z3GetExprForValue(PHI->getIncomingValue(i), C, ValMap);
        PhiExpr = PhiExpr || (ResultVar == Incoming);
      }
      return PhiExpr;
    }
  }

  if (auto *SI = dyn_cast<SelectInst>(&I)) {
    z3::expr ResultVar = makeFreshZ3Expr(&I, C);
    z3::expr TVal = z3GetExprForValue(SI->getTrueValue(), C, ValMap);
    z3::expr FVal = z3GetExprForValue(SI->getFalseValue(), C, ValMap);
    return (ResultVar == TVal) || (ResultVar == FVal);
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

  if (const auto *CI = dyn_cast<ConstantInt>(V)) {
    if (CI->getType()->isIntegerTy(1))
      return C.bool_val(CI->isOne());
    return C.int_val(CI->getSExtValue());
  }

  if (isa<Argument>(V)) {
    z3::expr E = makeFreshZ3Expr(V, C);
    ValMap.emplace(V, E);
    return E;
  }

  if (auto *I = dyn_cast<Instruction>(V)) {
    z3::expr E = computeInstructionExprZ3(*I, C, ValMap);
    ValMap.insert(std::make_pair(V, E));
    return E;
  }

  z3::expr Fallback = makeFreshZ3Expr(V, C);
  ValMap.insert(std::make_pair(V, Fallback));
  return Fallback;
}

void CFGMappingWitness::initialize(Function *Func, z3::context *C) {
  F = Func;
  SymCtx = C;
  SrcStringMap.clear();
  TgtStringMap.clear();
  SrcZ3Map.clear();
  TgtZ3Map.clear();
  SrcBlockValues.clear();
  TgtBlockValues.clear();
}

void CFGMappingWitness::buildSourceMappings() {
  errs() << "\n*** CFGMappingWitness::buildSourceMappings() ***\n";

  SrcStringMap = buildBlockStringValueMap();
  SrcZ3Map = buildBlockZ3ValueMap();

  dumpSourceStringMappings("Pre-Simplify Source String Mappings");
  dumpSourceZ3Mappings("Pre-Simplify Source Z3 Mappings");
}

void CFGMappingWitness::buildTargetMappings() {
  errs() << "\n*** CFGMappingWitness::buildTargetMappings() ***\n";

  TgtStringMap = buildBlockStringValueMap();
  TgtZ3Map = buildBlockZ3ValueMap();

  dumpTargetStringMappings("Post-Simplify Target String Mappings");
  dumpTargetZ3Mappings("Post-Simplify Target Z3 Mappings");
}

StringBlockMap CFGMappingWitness::buildBlockStringValueMap() {
  StringBlockMap Result;

  for (BasicBlock &BB : *F) {
    std::map<const Value *, std::string> BlockMap;

    // Add function arguments
    for (Argument &A : F->args()) {
      std::string Name = getValueName(&A);
      BlockMap[&A] = Name;
    }

    // Add instructions in this block
    DenseMap<const Value *, std::string> ValMap;
    for (Argument &A : F->args())
      ValMap[&A] = getValueName(&A);

    for (Instruction &I : BB) {
      if (I.getType()->isVoidTy())
        continue;
      std::string Expr = getExprForValue(&I, ValMap);
      BlockMap[&I] = Expr;
    }

    Result[&BB] = BlockMap;
    SrcBlockValues[&BB] = BlockMap;
  }

  return Result;
}

Z3BlockMap CFGMappingWitness::buildBlockZ3ValueMap() {
  Z3BlockMap Result;
  std::map<const Value *, z3::expr> GlobalMap;

  // Add function arguments
  for (Argument &A : F->args())
    GlobalMap.emplace(&A, makeFreshZ3Expr(&A, *SymCtx));

  // Add instructions per block
  for (BasicBlock &BB : *F) {
    std::map<const Value *, z3::expr> LocalMap = GlobalMap;

    for (Instruction &I : BB) {
      if (I.getType()->isVoidTy())
        continue;
      z3::expr E = computeInstructionExprZ3(I, *SymCtx, LocalMap);
      LocalMap.insert(std::make_pair(&I, E));
    }

    Result.insert(std::make_pair(&BB, LocalMap));
  }

  return Result;
}

void CFGMappingWitness::dumpSourceStringMappings(const char *Label) {
  errs() << "\n=== " << Label << " ===\n";
  for (BasicBlock &BB : *F) {
    errs() << "  {" << (BB.hasName() ? BB.getName() : "(unnamed)") << ": {";
    auto It = SrcStringMap.find(&BB);
    bool First = true;
    if (It != SrcStringMap.end()) {
      for (const auto &KV : It->second) {
        if (!First)
          errs() << ", ";
        errs() << "\"" << getValueName(KV.first) << "\": \""
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
    errs() << "  {" << (BB.hasName() ? BB.getName() : "(unnamed)") << ": {";
    auto It = SrcZ3Map.find(&BB);
    bool First = true;
    if (It != SrcZ3Map.end()) {
      for (const auto &KV : It->second) {
        if (!First)
          errs() << ", ";
        errs() << "\"" << getValueName(KV.first) << "\": \""
               << escapeValueString(Z3_ast_to_string(*SymCtx, KV.second))
               << "\"";
        First = false;
      }
    }
    errs() << "}}\n";
  }
}

void CFGMappingWitness::dumpTargetStringMappings(const char *Label) {
  errs() << "\n=== " << Label << " ===\n";
  for (BasicBlock &BB : *F) {
    errs() << "  {" << (BB.hasName() ? BB.getName() : "(unnamed)") << ": {";
    auto It = TgtStringMap.find(&BB);
    bool First = true;
    if (It != TgtStringMap.end()) {
      for (const auto &KV : It->second) {
        if (!First)
          errs() << ", ";
        errs() << "\"" << getValueName(KV.first) << "\": \""
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
    errs() << "  {" << (BB.hasName() ? BB.getName() : "(unnamed)") << ": {";
    auto It = TgtZ3Map.find(&BB);
    bool First = true;
    if (It != TgtZ3Map.end()) {
      for (const auto &KV : It->second) {
        if (!First)
          errs() << ", ";
        errs() << "\"" << getValueName(KV.first) << "\": \""
               << escapeValueString(Z3_ast_to_string(*SymCtx, KV.second))
               << "\"";
        First = false;
      }
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
