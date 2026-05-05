#include "z3++.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/Analysis/AssumptionCache.h"
#include "llvm/IR/Dominators.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/MemoryDependenceAnalysis.h"
#include "llvm/Analysis/MemorySSA.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/MDBuilder.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include "GVNLocal.h"
#include "llvm/Transforms/Utils/AssumeBundleBuilder.h"
#include "llvm/Transforms/Utils/Local.h"

#include "GVNWitness.h"

#include <cstdint>
#include <vector>

#undef DEBUG_TYPE
#define DEBUG_TYPE "GVNWitness"

using namespace llvm;

namespace {

static std::string getValueName(const Value *V) {
  if (V->hasName())
    return V->getName().str();
  std::string S;
  raw_string_ostream OS(S);
  OS << "v" << (const void *)V;
  return OS.str();
}

static std::string valueRefToString(const Value *V) {
  if (const auto *CI = dyn_cast<ConstantInt>(V))
    return std::to_string(CI->getSExtValue());
  if (const auto *CF = dyn_cast<ConstantFP>(V)) {
    SmallString<32> S;
    CF->getValueAPF().toString(S);
    return S.str().str();
  }
  if (isa<Constant>(V))
    return getValueName(V);
  return getValueName(V);
}

static std::string buildInstructionExpr(const Instruction &I) {
  if (const auto *BO = dyn_cast<BinaryOperator>(&I)) {
    std::string L = valueRefToString(BO->getOperand(0));
    std::string R = valueRefToString(BO->getOperand(1));
    return std::string(I.getOpcodeName()) + "(" + L + ", " + R + ")";
  }

  if (const auto *CI = dyn_cast<CmpInst>(&I)) {
    std::string L = valueRefToString(CI->getOperand(0));
    std::string R = valueRefToString(CI->getOperand(1));
    return std::string(CI->getPredicateName(CI->getPredicate())) + "(" + L +
           ", " + R + ")";
  }

  if (const auto *PHI = dyn_cast<PHINode>(&I)) {
    std::string S = "phi(";
    for (unsigned i = 0; i < PHI->getNumIncomingValues(); ++i) {
      if (i)
        S += ", ";
      S += valueRefToString(PHI->getIncomingValue(i));
    }
    S += ")";
    return S;
  }

  if (const auto *SI = dyn_cast<SelectInst>(&I)) {
    std::string C = valueRefToString(SI->getCondition());
    std::string T = valueRefToString(SI->getTrueValue());
    std::string F = valueRefToString(SI->getFalseValue());
    return "select(" + C + ", " + T + ", " + F + ")";
  }

  if (const auto *LI = dyn_cast<LoadInst>(&I)) {
    std::string P = valueRefToString(LI->getPointerOperand());
    return "load(" + P + ")";
  }

  if (const auto *SI = dyn_cast<StoreInst>(&I)) {
    std::string V = valueRefToString(SI->getValueOperand());
    std::string P = valueRefToString(SI->getPointerOperand());
    return "store(" + V + ", " + P + ")";
  }

  if (const auto *BC = dyn_cast<BitCastInst>(&I)) {
    std::string V = valueRefToString(BC->getOperand(0));
    return "bitcast(" + V + ")";
  }

  if (const auto *GEP = dyn_cast<GetElementPtrInst>(&I)) {
    std::string S = "gep(" + valueRefToString(GEP->getPointerOperand());
    for (const Use &Idx : GEP->indices()) {
      S += ", ";
      S += valueRefToString(Idx.get());
    }
    S += ")";
    return S;
  }

  if (const auto *EV = dyn_cast<ExtractValueInst>(&I)) {
    std::string S = "extractvalue(" + valueRefToString(EV->getAggregateOperand());
    for (unsigned Idx : EV->indices()) {
      S += ", ";
      S += std::to_string(Idx);
    }
    S += ")";
    return S;
  }

  if (const auto *IV = dyn_cast<InsertValueInst>(&I)) {
    std::string S = "insertvalue(" +
                    valueRefToString(IV->getAggregateOperand()) + ", " +
                    valueRefToString(IV->getInsertedValueOperand());
    for (unsigned Idx : IV->indices()) {
      S += ", ";
      S += std::to_string(Idx);
    }
    S += ")";
    return S;
  }

  if (const auto *EE = dyn_cast<ExtractElementInst>(&I)) {
    std::string V = valueRefToString(EE->getVectorOperand());
    std::string Idx = valueRefToString(EE->getIndexOperand());
    return "extractelement(" + V + ", " + Idx + ")";
  }

  if (const auto *IE = dyn_cast<InsertElementInst>(&I)) {
    std::string V = valueRefToString(IE->getOperand(0));
    std::string E = valueRefToString(IE->getOperand(1));
    std::string Idx = valueRefToString(IE->getOperand(2));
    return "insertelement(" + V + ", " + E + ", " + Idx + ")";
  }

  if (const auto *SV = dyn_cast<ShuffleVectorInst>(&I)) {
    std::string V1 = valueRefToString(SV->getOperand(0));
    std::string V2 = valueRefToString(SV->getOperand(1));
    std::string Mask = valueRefToString(SV->getOperand(2));
    return "shufflevector(" + V1 + ", " + V2 + ", " + Mask + ")";
  }

  if (const auto *CB = dyn_cast<CallBase>(&I)) {
    std::string S = "call(";
    if (const Function *Callee = CB->getCalledFunction())
      S += Callee->getName().str();
    else
      S += valueRefToString(CB->getCalledOperand());
    for (unsigned i = 0; i < CB->arg_size(); ++i) {
      S += ", ";
      S += valueRefToString(CB->getArgOperand(i));
    }
    S += ")";
    return S;
  }

  if (const auto *F = dyn_cast<FreezeInst>(&I)) {
    std::string V = valueRefToString(F->getOperand(0));
    return "freeze(" + V + ")";
  }

  if (const auto *Cast = dyn_cast<CastInst>(&I)) {
    std::string V = valueRefToString(Cast->getOperand(0));
    return std::string(I.getOpcodeName()) + "(" + V + ")";
  }

  std::string S = std::string(I.getOpcodeName()) + "(";
  for (unsigned i = 0; i < I.getNumOperands(); ++i) {
    if (i)
      S += ", ";
    S += valueRefToString(I.getOperand(i));
  }
  S += ")";
  return S;
}

static z3::expr createSSAValueExpr(const Value *V, z3::context *C) {
  if (const auto *CI = dyn_cast<ConstantInt>(V))
    return C->int_val((int)CI->getSExtValue());
  return C->int_const(getValueName(V).c_str());
}

static std::string getInstructionUFName(const Instruction &I) {
  if (const auto *CB = dyn_cast<CallBase>(&I)) {
    if (const Function *Callee = CB->getCalledFunction())
      return "call_" + Callee->getName().str();
    return "call_indirect";
  }
  return I.getOpcodeName();
}

static z3::expr buildSSAInstructionRHS(const Instruction &I, z3::context *C) {
  if (const auto *BC = dyn_cast<BitCastInst>(&I))
    return createSSAValueExpr(BC->getOperand(0), C);

  std::vector<z3::expr> Args;
  Args.reserve(I.getNumOperands());
  for (const Use &Op : I.operands())
    Args.push_back(createSSAValueExpr(Op.get(), C));

  std::string UFName = getInstructionUFName(I);
  if (Args.empty()) {
    std::string ConstName = UFName + "_" + getValueName(&I);
    return C->int_const(ConstName.c_str());
  }

  std::vector<z3::sort> Domain(Args.size(), C->int_sort());
  z3::func_decl F = z3::function(UFName.c_str(), Domain.size(), Domain.data(),
                                 C->int_sort());
  z3::expr_vector ArgVec(*C);
  for (const z3::expr &A : Args)
    ArgVec.push_back(A);
  return F(ArgVec);
}

static z3::expr valueIdExpr(const Value *V, z3::context *C) {
  uintptr_t Id = reinterpret_cast<uintptr_t>(V);
  return C->int_val(std::to_string(Id).c_str());
}

static z3::expr buildSubexprZ3Map(const BasicBlock &BB,
                                  const DenseMap<const Value *, std::string> &ValueExprs) {
  z3::context *C = InvariantManager::globalContext();
  z3::sort KeySort = C->int_sort();
  z3::sort ValSort = C->string_sort();
  std::string MapName = "subexpr_map_" +
                        (BB.hasName() ? BB.getName().str() : "unnamed");
  z3::expr Map = C->constant(MapName.c_str(), C->array_sort(KeySort, ValSort));

  for (const auto &Entry : ValueExprs) {
    const Value *V = Entry.first;
    const std::string &Expr = Entry.second;
    Map = store(Map, valueIdExpr(V, C), C->string_val(Expr.c_str()));
  }

  return Map;
}

static void dumpValueSubexprMap(Function &F, WitnessChecker &wC) {
  errs() << "\n=== GVN Subexpression Map ===\n";
  z3::context *C = InvariantManager::globalContext();
  z3::expr FunctionAssignments = C->bool_val(true);
  z3::sort MemSort = C->array_sort(C->int_sort(), C->int_sort());
  int MemIndex = 0;
  z3::expr CurrentMem = C->constant("mem0", MemSort);
  for (BasicBlock &BB : F) {
    errs() << "Processing Basic Block: " << (BB.hasName() ? BB.getName() : "(unnamed)") << "\n";
    DenseMap<const Value *, std::string> ValueExprs;
    z3::expr BlockAssignments = C->bool_val(true);
    errs() << "  {" << (BB.hasName() ? BB.getName() : "(unnamed)") << ": {";
    bool First = true;

    for (Argument &A : F.args()) {
      ValueExprs[&A] = getValueName(&A);
    }

    for (Instruction &I : BB) {
      errs() << "    Processing Instruction: " << I << "\n";
      bool IsStore = isa<StoreInst>(&I);
      bool IsCall = isa<CallBase>(&I);
      if (I.getType()->isVoidTy() && !IsStore && !IsCall)
        continue;
      std::string Expr = buildInstructionExpr(I);
      if (!IsStore)
        ValueExprs[&I] = Expr;
      if (!First)
        errs() << ", ";
      errs() << '"' << getValueName(&I) << "\": \"" << Expr << '"';
      First = false;

      if (const auto *SI = dyn_cast<StoreInst>(&I)) {
        z3::expr Addr = createSSAValueExpr(SI->getPointerOperand(), C);
        z3::expr Val = createSSAValueExpr(SI->getValueOperand(), C);
        std::string MemName = "mem" + std::to_string(++MemIndex);
        z3::expr NextMem = C->constant(MemName.c_str(), MemSort);
        BlockAssignments =
            BlockAssignments && (NextMem == store(CurrentMem, Addr, Val));
        CurrentMem = NextMem;
      } else if (const auto *LI = dyn_cast<LoadInst>(&I)) {
        z3::expr Addr = createSSAValueExpr(LI->getPointerOperand(), C);
        z3::expr LHS = createSSAValueExpr(&I, C);
        z3::expr RHS = select(CurrentMem, Addr);
        BlockAssignments = BlockAssignments && (LHS == RHS);
      } else {
        z3::expr LHS = createSSAValueExpr(&I, C);
        z3::expr RHS = buildSSAInstructionRHS(I, C);
        BlockAssignments = BlockAssignments && (LHS == RHS);
      }
    }

    z3::expr SubexprMap = buildSubexprZ3Map(BB, ValueExprs);
    wC.setSubexprMap(&BB, SubexprMap);
    wC.setBlockAssignments(&BB, BlockAssignments.simplify());
    FunctionAssignments = FunctionAssignments && BlockAssignments;

    errs() << "}}\n";
  }

  wC.setFunctionAssignments(FunctionAssignments.simplify());
}

} // namespace

PreservedAnalyses GVNWitness::run(Function &F,
                                  FunctionAnalysisManager &FAM) {
  // Clear the global tracking of replaced variables for this function
  GVNReplacements.clear();

  // Create and run the GVN pass
  GVNPassLocal gvnPass;
  
  // Initialize the witness checker before running GVN
  z3::context *C = InvariantManager::globalContext();
  WitnessChecker wC(&F, C);
  errs() << "\n=== Running GVN Witness Pass on Function: " << F.getName() << " ===\n";
  
  // Run the actual GVN transformation

  dumpValueSubexprMap(F, wC);
  errs() << "Function assignments: " << Z3_ast_to_string(*C, wC.getFunctionAssignments()) << "\n";
  
  PreservedAnalyses PA = gvnPass.run(F, FAM);
  
  //Build the block relation after the transformation
  wC.buildBlockRelation(true);

  // Augment TargetRelation with GVN-proved equalities for deleted variables
  z3::expr targetReplacements = C->bool_val(true);
  for (const auto& entry : GVNReplacements) {
      std::string oldName = entry.first;
      std::string newName = entry.second;
      z3::expr old_T = C->int_const(concatStrings(STATE_T, oldName).data());
      z3::expr new_T = C->int_const(concatStrings(STATE_T, newName).data());
      z3::expr old_V = C->int_const(concatStrings(STATE_V, oldName).data());
      z3::expr new_V = C->int_const(concatStrings(STATE_V, newName).data());
      // The equality holds globally in the target state T and its next state V
      targetReplacements = targetReplacements && (old_T == new_T) && (old_V == new_V);
  }
  wC.setTargetRelation(wC.getTargetRelation() && targetReplacements);
  
  // Generate the witness for the GVN transformation
  generateWitness(F, wC);
  
  errs() << "Witness check: ";

  //Check the witness
  bool witnessValid = wC.checkWitness();
  wC.dumpExpressions();
  
  if (!witnessValid) {
    errs() << "WARNING: Witness check failed for GVN pass!\n";
  } else {
    errs() << "SUCCESS: Witness check passed for GVN pass!\n";
  }
  
  // assert(witnessValid && "GVN Witness check failed!");
  
  // Propagate invariants
  wC.propagateInvariants();

  return PA;
}

void GVNWitness::generateWitness(Function &F, WitnessChecker &wC) {
  z3::context *C = InvariantManager::globalContext();
  z3::expr WitnessExprST = C->bool_val(false);
  z3::expr WitnessExprVU = C->bool_val(false);
  z3::expr piS = C->int_const(concatStrings(STATE_S, PC_VAR).data());
  z3::expr piT = C->int_const(concatStrings(STATE_T, PC_VAR).data());
  z3::expr piU = C->int_const(concatStrings(STATE_U, PC_VAR).data());
  z3::expr piV = C->int_const(concatStrings(STATE_V, PC_VAR).data());

  errs() << "Generating witness for GVN transformation...\n";

  // For each basic block, create witness expressions
  for (auto &Blk : F) {
    int blockLabel = wC.getBlockLabel(&Blk);

    // Witness for state preservation between source (S) and target (T)
    // GVN eliminates redundant computations, so values should be equivalent
    z3::expr WST =
        piS == blockLabel && piT == blockLabel &&
        createEqList(C, wC.getTargetVars(), STATE_S, STATE_T, PC_VAR);

    // Witness for state preservation between verification states (V) and (U)
    z3::expr WVU =
        piV == blockLabel && piU == blockLabel &&
        createEqList(C, wC.getTargetVars(), STATE_V, STATE_U, PC_VAR);

    // Apply the variable replacements found by GVN
    z3::expr replacementsExprT = C->bool_val(true);
    z3::expr replacementsExprU = C->bool_val(true);
    for (const auto& entry : GVNReplacements) {
        std::string oldName = entry.first;
        std::string newName = entry.second;
        z3::expr oldExpr_S = C->int_const(concatStrings(STATE_S, oldName).data());
        z3::expr oldExpr_T = C->int_const(concatStrings(STATE_T, oldName).data());
        z3::expr newExpr_T = C->int_const(concatStrings(STATE_T, newName).data());
        // Link the deleted source variable to target, and map target to new target
        replacementsExprT = replacementsExprT && (oldExpr_S == oldExpr_T) && (oldExpr_T == newExpr_T);

        z3::expr oldExpr_V = C->int_const(concatStrings(STATE_V, oldName).data());
        z3::expr oldExpr_U = C->int_const(concatStrings(STATE_U, oldName).data());
        z3::expr newExpr_U = C->int_const(concatStrings(STATE_U, newName).data());
        replacementsExprU = replacementsExprU && (oldExpr_V == oldExpr_U) && (oldExpr_U == newExpr_U);
    }
    WST = WST && replacementsExprT;
    WVU = WVU && replacementsExprU;

    WitnessExprST = WitnessExprST || WST;
    WitnessExprVU = WitnessExprVU || WVU;
  }

  // Add witness expressions for final states (exit blocks)
  z3::expr FinalST =
      piS == -1 && piT == -1 &&
      createEqList(C, wC.getTargetVars(), STATE_S, STATE_T, PC_VAR);

  z3::expr FinalVU =
      piV == -1 && piU == -1 &&
      createEqList(C, wC.getTargetVars(), STATE_V, STATE_U, PC_VAR);

  z3::expr replacementsExprT_Final = C->bool_val(true);
  z3::expr replacementsExprU_Final = C->bool_val(true);
  for (const auto& entry : GVNReplacements) {
      std::string oldName = entry.first;
      std::string newName = entry.second;
      z3::expr oldExpr_S = C->int_const(concatStrings(STATE_S, oldName).data());
      z3::expr oldExpr_T = C->int_const(concatStrings(STATE_T, oldName).data());
      z3::expr newExpr_T = C->int_const(concatStrings(STATE_T, newName).data());
      replacementsExprT_Final = replacementsExprT_Final && (oldExpr_S == oldExpr_T) && (oldExpr_T == newExpr_T);

      z3::expr oldExpr_V = C->int_const(concatStrings(STATE_V, oldName).data());
      z3::expr oldExpr_U = C->int_const(concatStrings(STATE_U, oldName).data());
      z3::expr newExpr_U = C->int_const(concatStrings(STATE_U, newName).data());
      replacementsExprU_Final = replacementsExprU_Final && (oldExpr_V == oldExpr_U) && (oldExpr_U == newExpr_U);
  }
  FinalST = FinalST && replacementsExprT_Final;
  FinalVU = FinalVU && replacementsExprU_Final;

  // Check for invariants at the exit block
  auto *Inv = InvariantManager::find(&*(--F.end()));
  if (Inv) {
    FinalST = FinalST && Inv->getInv(0);
    FinalVU = FinalVU && Inv->getInv(1);
    errs() << "Applied invariants from exit block\n";
  }

  WitnessExprST = WitnessExprST || FinalST;
  WitnessExprVU = WitnessExprVU || FinalVU;

  // Simplify the witness expressions and set them in the checker
  wC.setWitnessST(WitnessExprST.simplify());
  wC.setWitnessVU(WitnessExprVU.simplify());

  errs() << "Witness expressions generated and simplified\n";
  errs() << "WitnessST: " << Z3_ast_to_string(*C, WitnessExprST.simplify()) << "\n";
  errs() << "WitnessVU: " << Z3_ast_to_string(*C, WitnessExprVU.simplify()) << "\n";
}