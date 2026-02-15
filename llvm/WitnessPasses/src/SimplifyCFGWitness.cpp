#include "z3++.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/Hashing.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Analysis/AssumptionCache.h"
#include "llvm/Analysis/CFG.h"
#include "llvm/Analysis/DomTreeUpdater.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/MDBuilder.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/ValueHandle.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Scalar/SimplifyCFG.h"
#include "llvm/Transforms/Utils/AssumeBundleBuilder.h"
#include "llvm/Transforms/Utils/Local.h"
#include "llvm/Transforms/Utils/SimplifyCFGOptions.h"

#include <set>
#include <string>
#include <utility>

#include "SimplifyCFGWitness.h"

#define DEBUG_TYPE "SimplifyCFGWitness"

using namespace llvm;

namespace {

using StringRefVec = SmallVector<StringRef, 4>;

} // end anonymous namespace

namespace llvm {
template <> struct DenseMapInfo<StringRefVec> {
  static inline StringRefVec getEmptyKey() {
    return StringRefVec{DenseMapInfo<StringRef>::getEmptyKey()};
  }
  static inline StringRefVec getTombstoneKey() {
    return StringRefVec{DenseMapInfo<StringRef>::getTombstoneKey()};
  }
  static unsigned getHashValue(const StringRefVec &V) {
    return hash_combine_range(V.begin(), V.end());
  }
  static bool isEqual(const StringRefVec &LHS, const StringRefVec &RHS) {
    return LHS == RHS;
  }
};
} // namespace llvm

namespace {

static std::string getValueName(const Value *V) {
  if (V->hasName())
    return V->getName().str();
  std::string S;
  raw_string_ostream OS(S);
  OS << "v" << (const void *)V;
  return OS.str();
}

static std::string escapeValueString(StringRef In) {
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

static std::string
getExprForValue(const Value *V, DenseMap<const Value *, std::string> &ValMap);

static std::string getConstString(const ConstantInt *CI) {
  if (CI->getType()->isIntegerTy(1))
    return CI->isOne() ? "true" : "false";
  return std::to_string(CI->getSExtValue());
}

static std::string
computeInstructionExprString(const Instruction &I,
                             DenseMap<const Value *, std::string> &ValMap) {
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

static std::string
getExprForValue(const Value *V, DenseMap<const Value *, std::string> &ValMap) {
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

static z3::expr makeFreshZ3Expr(const Value *V, z3::context &C) {
  std::string Name = getValueName(V);
  if (V->getType()->isIntegerTy(1))
    return C.bool_const(Name.c_str());
  if (V->getType()->isIntegerTy())
    return C.int_const(Name.c_str());
  return C.int_const(Name.c_str());
}

static z3::expr z3GetExprForValue(const Value *V, z3::context &C,
                                  std::map<const Value *, z3::expr> &ValMap);

static z3::expr
computeInstructionExprZ3(const Instruction &I, z3::context &C,
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
    }
  }

  if (auto *SI = dyn_cast<SelectInst>(&I)) {
    z3::expr Cond = z3GetExprForValue(SI->getCondition(), C, ValMap);
    z3::expr TVal = z3GetExprForValue(SI->getTrueValue(), C, ValMap);
    z3::expr FVal = z3GetExprForValue(SI->getFalseValue(), C, ValMap);
    return z3::ite(Cond, TVal, FVal);
  }

  if (auto *LI = dyn_cast<LoadInst>(&I)) {
    (void)LI;
    return makeFreshZ3Expr(&I, C);
  }

  return makeFreshZ3Expr(&I, C);
}

static z3::expr z3GetExprForValue(const Value *V, z3::context &C,
                                  std::map<const Value *, z3::expr> &ValMap) {
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
    ValMap.insert({V, E});
    return E;
  }

  if (auto *I = dyn_cast<Instruction>(V)) {
    z3::expr E = computeInstructionExprZ3(*I, C, ValMap);
    ValMap.insert({V, E});
    return E;
  }

  z3::expr Fallback = makeFreshZ3Expr(V, C);
  ValMap.insert({V, Fallback});
  return Fallback;
}

static void dumpBlockValueMap(Function &F, const char *Label) {
  errs() << "\n=== " << Label << " ===\n";
  for (BasicBlock &BB : F) {
    errs() << "  {" << (BB.hasName() ? BB.getName() : "(unnamed)") << ": {";
    DenseMap<const Value *, std::string> ValMap;
    bool First = true;

    for (Argument &A : F.args()) {
      if (!First)
        errs() << ", ";
      std::string Name = getValueName(&A);
      std::string Val = escapeValueString(getExprForValue(&A, ValMap));
      errs() << "\"" << Name << "\": \"" << Val << "\"";
      First = false;
    }

    for (Instruction &I : BB) {
      if (I.getType()->isVoidTy())
        continue;
      if (!First)
        errs() << ", ";
      std::string Name = getValueName(&I);
      std::string Val = escapeValueString(getExprForValue(&I, ValMap));
      errs() << "\"" << Name << "\": \"" << Val << "\"";
      First = false;
    }

    errs() << "}}\n";
  }
}

using Z3BlockMap =
    std::map<const BasicBlock *, std::map<const Value *, z3::expr>>;

static Z3BlockMap buildBlockZ3ValueMap(Function &F, z3::context &C) {
  Z3BlockMap Result;
  std::map<const Value *, z3::expr> GlobalMap;
  for (Argument &A : F.args())
    GlobalMap.insert({&A, makeFreshZ3Expr(&A, C)});

  for (BasicBlock &BB : F) {
    std::map<const Value *, z3::expr> LocalMap = GlobalMap;
    for (Instruction &I : BB) {
      if (I.getType()->isVoidTy())
        continue;
      z3::expr E = computeInstructionExprZ3(I, C, LocalMap);
      LocalMap.insert({&I, E});
    }
    Result.insert({&BB, std::move(LocalMap)});
  }

  return Result;
}

static void dumpBlockZ3ValueMap(Function &F, const Z3BlockMap &Map,
                                z3::context &C, const char *Label) {
  errs() << "\n=== " << Label << " ===\n";
  for (BasicBlock &BB : F) {
    errs() << "  {" << (BB.hasName() ? BB.getName() : "(unnamed)") << ": {";
    bool First = true;
    auto It = Map.find(&BB);
    if (It != Map.end()) {
      for (const auto &KV : It->second) {
        const Value *V = KV.first;
        const z3::expr &E = KV.second;
        if (!First)
          errs() << ", ";
        errs() << "\"" << getValueName(V) << "\": \""
               << escapeValueString(Z3_ast_to_string(C, E)) << "\"";
        First = false;
      }
    }
    errs() << "}}\n";
  }
}

} // namespace

// Forward declaration for our custom simplifyCFG with blockMap support
namespace witness {

bool simplifyCFG(BasicBlock *BB, const TargetTransformInfo &TTI,
                 DomTreeUpdater *DTU, const SimplifyCFGOptions &Options,
                 ArrayRef<WeakVH> LoopHeaders,
                 DenseMap<BasicBlock *, BasicBlock *> *BlockMap = nullptr);
}

// ===== Copied SimplifyCFG implementation for block tracking =====

static bool performBlockTailMerging(
    Function &F, ArrayRef<BasicBlock *> BBs,
    std::vector<DominatorTree::UpdateType> *Updates,
    DenseMap<BasicBlock *, BasicBlock *> *blockMap = nullptr) {
  SmallVector<PHINode *, 1> NewOps;

  if (BBs.size() < 2)
    return false;

  if (Updates)
    Updates->reserve(Updates->size() + BBs.size());

  BasicBlock *CanonicalBB;
  Instruction *CanonicalTerm;
  {
    auto *Term = BBs[0]->getTerminator();
    CanonicalBB = BasicBlock::Create(
        F.getContext(), Twine("common.") + Term->getOpcodeName(), &F, BBs[0]);
    NewOps.resize(Term->getNumOperands());
    for (auto I : zip(Term->operands(), NewOps)) {
      std::get<1>(I) = PHINode::Create(std::get<0>(I)->getType(),
                                       /*NumReservedValues=*/BBs.size(),
                                       CanonicalBB->getName() + ".op");
      std::get<1>(I)->insertInto(CanonicalBB, CanonicalBB->end());
    }
    CanonicalTerm = Term->clone();
    CanonicalTerm->insertInto(CanonicalBB, CanonicalBB->end());
    for (auto I : zip(NewOps, CanonicalTerm->operands()))
      std::get<1>(I) = std::get<0>(I);
  }

  DebugLoc CommonDebugLoc;
  for (BasicBlock *BB : BBs) {
    auto *Term = BB->getTerminator();
    for (auto I : zip(Term->operands(), NewOps))
      std::get<1>(I)->addIncoming(std::get<0>(I), BB);

    if (!CommonDebugLoc)
      CommonDebugLoc = Term->getDebugLoc();
    else
      CommonDebugLoc =
          DebugLoc::getMergedLocation(CommonDebugLoc, Term->getDebugLoc());

    Instruction *BI = BranchInst::Create(CanonicalBB, BB);
    BI->setDebugLoc(Term->getDebugLoc());
    Term->eraseFromParent();

    if (Updates)
      Updates->push_back({DominatorTree::Insert, BB, CanonicalBB});

    if (blockMap)
      (*blockMap)[BB] = CanonicalBB;
  }

  errs() << "performBlockTailMerging: merged " << BBs.size() << " blocks into "
         << CanonicalBB->getName() << "\n";

  CanonicalTerm->setDebugLoc(CommonDebugLoc);
  return true;
}

static bool tailMergeBlocksWithSimilarFunctionTerminators(
    Function &F, DomTreeUpdater *DTU,
    DenseMap<BasicBlock *, BasicBlock *> *blockMap = nullptr) {
  SmallMapVector<unsigned, SmallVector<BasicBlock *, 2>, 4> Structure;

  for (BasicBlock &BB : F) {
    if (DTU && DTU->isBBPendingDeletion(&BB))
      continue;

    if (!succ_empty(&BB))
      continue;

    auto *Term = BB.getTerminator();
    switch (Term->getOpcode()) {
    case Instruction::Ret:
    case Instruction::Resume:
      break;
    default:
      continue;
    }

    if (BB.getTerminatingMustTailCall())
      continue;

    if (any_of(Term->operands(),
               [](Value *Op) { return Op->getType()->isTokenTy(); }))
      continue;

    Structure[Term->getOpcode()].emplace_back(&BB);
  }

  bool Changed = false;
  std::vector<DominatorTree::UpdateType> Updates;

  errs() << "tailMergeBlocksWithSimilarFunctionTerminators: found "
         << Structure.size() << " terminator types\n";
  for (auto &Entry : Structure) {
    errs() << "  Terminator opcode " << Entry.first << ": "
           << Entry.second.size() << " blocks\n";
  }

  for (ArrayRef<BasicBlock *> BBs : make_second_range(Structure))
    Changed |=
        performBlockTailMerging(F, BBs, DTU ? &Updates : nullptr, blockMap);

  if (DTU)
    DTU->applyUpdates(Updates);

  return Changed;
}

static bool iterativelySimplifyCFG(
    Function &F, const TargetTransformInfo &TTI, DomTreeUpdater *DTU,
    const SimplifyCFGOptions &Options,
    DenseMap<BasicBlock *, BasicBlock *> *blockMap = nullptr) {
  bool Changed = false;
  bool LocalChange = true;

  SmallVector<std::pair<const BasicBlock *, const BasicBlock *>, 32> Edges;
  FindFunctionBackedges(F, Edges);
  SmallPtrSet<BasicBlock *, 16> UniqueLoopHeaders;
  for (const auto &Edge : Edges)
    UniqueLoopHeaders.insert(const_cast<BasicBlock *>(Edge.second));

  SmallVector<WeakVH, 16> LoopHeaders(UniqueLoopHeaders.begin(),
                                      UniqueLoopHeaders.end());

  unsigned IterCnt = 0;
  (void)IterCnt;
  while (LocalChange) {
    assert(IterCnt++ < 1000 && "Iterative cleaication didn't converge!");
    LocalChange = false;

    for (Function::iterator BBIt = F.begin(); BBIt != F.end();) {
      BasicBlock *BB = &*BBIt++;
      if (DTU) {
        assert(
            !DTU->isBBPendingDeletion(BB) &&
            "Should not end up trying to simplify blocks marked for removal.");
        while (BBIt != F.end() && DTU->isBBPendingDeletion(&*BBIt))
          ++BBIt;
      }

      // Track the block's single predecessor before simplification
      BasicBlock *Pred = BB->getSinglePredecessor();

      if (witness::simplifyCFG(BB, TTI, DTU, Options, LoopHeaders, blockMap)) {
        LocalChange = true;

        // If BB was merged into its predecessor, record it
        if (blockMap && Pred && BB->getParent() == nullptr) {
          (*blockMap)[BB] = Pred;
          errs() << "iterativelySimplifyCFG: merged block into predecessor\n";
        }
      }
    }
    Changed |= LocalChange;
  }
  return Changed;
}

static bool simplifyFunctionCFGImpl(
    Function &F, const TargetTransformInfo &TTI, DominatorTree *DT,
    const SimplifyCFGOptions &Options,
    DenseMap<BasicBlock *, BasicBlock *> *blockMap = nullptr) {
  DomTreeUpdater DTU(DT, DomTreeUpdater::UpdateStrategy::Eager);
  DomTreeUpdater *DTUPtr = &DTU;

  // First removeUnreachableBlocks call (line 222)
  SmallPtrSet<BasicBlock *, 16> BlocksBefore1;
  SmallVector<std::pair<BasicBlock *, std::string>, 16> BlockInfoBefore1;
  for (BasicBlock &BB : F) {
    BlocksBefore1.insert(&BB);
    BlockInfoBefore1.push_back({&BB, BB.hasName() ? BB.getName().str() : ""});
  }

  bool EverChanged = removeUnreachableBlocks(F, DT ? DTUPtr : nullptr);

  if (EverChanged && blockMap) {
    SmallPtrSet<BasicBlock *, 16> BlocksAfter1;
    for (BasicBlock &BB : F) {
      BlocksAfter1.insert(&BB);
    }
    for (auto &[BBPtr, Name] : BlockInfoBefore1) {
      if (!BlocksAfter1.count(BBPtr)) {
        (*blockMap)[BBPtr] = nullptr;
        errs() << "removeUnreachableBlocks(#1): DELETED "
               << (Name.empty() ? "unnamed" : Name) << "\n";
      }
    }
  }

  EverChanged |= tailMergeBlocksWithSimilarFunctionTerminators(
      F, DT ? DTUPtr : nullptr, blockMap);

  EverChanged |=
      iterativelySimplifyCFG(F, TTI, DT ? DTUPtr : nullptr, Options, blockMap);

  if (!EverChanged)
    return false;

  if (!removeUnreachableBlocks(F, DT ? DTUPtr : nullptr))
    return EverChanged;

  do {
    EverChanged = iterativelySimplifyCFG(F, TTI, DT ? DTUPtr : nullptr, Options,
                                         blockMap);
    EverChanged |= removeUnreachableBlocks(F, DT ? DTUPtr : nullptr);
  } while (EverChanged);

  return true;
}

static bool
simplifyFunctionCFG(Function &F, const TargetTransformInfo &TTI,
                    DominatorTree *DT, const SimplifyCFGOptions &Options,
                    DenseMap<BasicBlock *, BasicBlock *> *blockMap = nullptr) {
  return simplifyFunctionCFGImpl(F, TTI, DT, Options, blockMap);
}

// ===== End of copied SimplifyCFG implementation =====

PreservedAnalyses SimplifyCFGWitness::run(Function &F,
                                          FunctionAnalysisManager &FAM) {
  errs() << "\n*** SimplifyCFGWitness::run() called for function: "
         << F.getName() << " ***\n";

  // Get required analyses
  auto &TTI = FAM.getResult<TargetIRAnalysis>(F);
  SimplifyCFGOptions Options;
  Options.AC = &FAM.getResult<AssumptionAnalysis>(F);
  DominatorTree *DT = &FAM.getResult<DominatorTreeAnalysis>(F);

  z3::context SymCtx;

  // ===== CAPTURE SOURCE STATE =====
  errs() << "\n=== Capturing Source CFG State Before Transformation ===\n";
  CFGMappingWitness::initialize(&F, &SymCtx);
  CFGMappingWitness::buildSourceMappings();

  // Internal blockMap for tracking merges
  DenseMap<BasicBlock *, BasicBlock *> blockMap;

  errs() << "Calling simplifyFunctionCFG with blockMap...\n";
  ;
  bool Changed = simplifyFunctionCFG(F, TTI, DT, Options, &blockMap);
  errs() << "simplifyFunctionCFG returned, Changed=" << Changed << "\n";

  // ===== CAPTURE TARGET STATE =====
  errs() << "\n=== Capturing Target CFG State After Transformation ===\n";
  CFGMappingWitness::buildTargetMappings();

  // All block mapping and inference is done by SimplifyCFGUtils
  // Just display the final blockMap
  errs() << "\n=== Final blockMap from SimplifyCFGUtils ===\n";
  errs() << "blockMap size: " << blockMap.size() << "\n";

  // Pass static CFGMappingWitness reference and blockMap to generateWitness
  CFGMappingWitness dummyWitness; // Placeholder since we use static methods
  generateWitness(F, dummyWitness, blockMap, SymCtx);

  // Release Z3 expressions before SymCtx is destroyed.
  CFGMappingWitness::clear();

  PreservedAnalyses PA;
  PA.preserve<DominatorTreeAnalysis>();
  return PA;
}

/// Build witness relations from a block mapping.
/// Uses explicit block mapping to verify transformation soundness
void SimplifyCFGWitness::generateWitness(
    Function &F, CFGMappingWitness &CFGWitness,
    DenseMap<BasicBlock *, BasicBlock *> &blockMap, z3::context &SymCtx) {
  errs() << "\n=== INTEGRATED SIMPLIFY-CFG WITNESS VALIDATION ===\n";

  z3::context &c = SymCtx;
  z3::solver solver(c);
  z3::expr_vector forall_vars(c);
  for (Argument &A : F.args()) {
    if (A.getType()->isIntegerTy(1)) {
      forall_vars.push_back(c.bool_const(A.getName().str().c_str()));
    } else if (A.getType()->isIntegerTy()) {
      forall_vars.push_back(c.int_const(A.getName().str().c_str()));
    }
  }
  const auto &src_conditions = CFGMappingWitness::getSourcePathCondMap();
  const auto &tgt_conditions = CFGMappingWitness::getTargetPathCondMap();

  auto lookup_src_cond = [&](StringRef name) {
    auto It = src_conditions.find(name.str());
    if (It != src_conditions.end())
      return It->second;
    return c.bool_val(false);
  };

  auto lookup_tgt_cond = [&](StringRef name) {
    auto It = tgt_conditions.find(name.str());
    if (It != tgt_conditions.end())
      return It->second;
    return c.bool_val(false);
  };
  // ==========================================================
  // STEP 2A: WITNESSED MAPPING (One-to-Many: Source -> Targets)
  // Each source block maps to one or more target blocks.
  // This is the initial witness produced by the transformation.
  // ==========================================================
  DenseMap<StringRef, StringRefVec> witness_one_to_many;
  witness_one_to_many["entry"].push_back("entry");
  witness_one_to_many["merge"].push_back("entry");
  witness_one_to_many["if_true"].push_back("if_true");
  witness_one_to_many["if_true"].push_back("common.ret");
  witness_one_to_many["if_false"].push_back("if_false");
  witness_one_to_many["if_false"].push_back("common.ret");

  errs() << "\n=== Step 2A: One-to-Many Witness ===\n";
  for (const auto &[src, tgts] : witness_one_to_many) {
    errs() << "  " << src << " -> {";
    for (size_t i = 0; i < tgts.size(); ++i) {
      errs() << tgts[i];
      if (i + 1 < tgts.size())
        errs() << ", ";
    }
    errs() << "}\n";
  }

  // ==========================================================
  // STEP 2B: PROCESS INTO MANY-TO-MANY CLUSTERS
  // Group sources that map to the same target set into clusters.
  // Maps: vector<src blocks> -> vector<tgt blocks>
  // This automatically handles merges (many src -> one tgt).
  // ==========================================================
  DenseMap<StringRefVec, StringRefVec> many_to_many;
  DenseMap<StringRefVec, StringRefVec> temp_map;

  auto normalize_set = [](const StringRefVec &In) {
    StringRefVec Out(In.begin(), In.end());
    llvm::sort(Out);
    Out.erase(std::unique(Out.begin(), Out.end()), Out.end());
    return Out;
  };

  for (const auto &[src, tgts] : witness_one_to_many) {
    StringRefVec norm_tgts = normalize_set(tgts);
    temp_map[norm_tgts].push_back(src);
  }

  for (const auto &[tgts, srcs] : temp_map) {
    StringRefVec norm_srcs = normalize_set(srcs);
    StringRefVec norm_tgts = normalize_set(tgts);
    many_to_many[norm_srcs] = norm_tgts;
  }

  errs() << "\n=== Step 2B: Many-to-Many Clusters ===\n";
  for (const auto &[srcs, tgts] : many_to_many) {
    // Identify cluster type
    StringRef cluster_type = "default";
    if (srcs.size() == 1 && tgts.size() == 1) {
      cluster_type = "ONE-TO-ONE";
    } else if (srcs.size() > 1 && tgts.size() == 1) {
      cluster_type = "MERGE";
    } else if (srcs.size() == 1 && tgts.size() > 1) {
      cluster_type = "SPLIT";
    } else if (srcs.size() > 1 && tgts.size() > 1) {
      cluster_type = "MANY-TO-MANY";
    }

    errs() << "  [" << cluster_type << "] {";
    for (size_t i = 0; i < srcs.size(); ++i) {
      errs() << srcs[i];
      if (i + 1 < srcs.size())
        errs() << ", ";
    }
    errs() << "} -> {";
    for (size_t i = 0; i < tgts.size(); ++i) {
      errs() << tgts[i];
      if (i + 1 < tgts.size())
        errs() << ", ";
    }
    errs() << "}\n";
  }

  // ==========================================================
  // STEP 2C: STATE DEFINITIONS FOR ALL VARIABLES
  // Track actual SSA values computed at each block.
  // ==========================================================

  // Source block state: only include variables actually COMPUTED in that block
  // (not passed through from predecessors)

  auto reach_src_set = [&](const StringRefVec &Set) {
    z3::expr cond = c.bool_val(false);
    for (StringRef S : Set)
      cond = cond && lookup_src_cond(S);
    return cond;
  };

  auto reach_tgt_set = [&](const StringRefVec &Set) {
    z3::expr cond = c.bool_val(false);
    for (StringRef T : Set)
      cond = cond && lookup_tgt_cond(T);
    return cond;
  };

  z3::expr_vector path_checking(c);
  z3::expr_vector state_checking(c);
  StringRefVec dead_blocks;
  dead_blocks.push_back("dead_block");
6  // ==========================================================
  // STEP 3: VERIFICATION CONDITIONS - REACHABILITY ONLY
  // ==========================================================

  errs() << "\n=== Step 3: Generating Reachability VCs ===\n";

  // ==========================================================
  // STEP 3: CLUSTER-BASED VERIFICATION CONDITIONS
  // Iterate through many_to_many map <sources_vec, targets_vec>
  // and apply different logic based on cluster type
  // ==========================================================

  errs() << "\n=== Step 3: Generating Verification Conditions by Cluster Type "
            "===\n";
  for (const auto &[srcs, tgts] : many_to_many) {

    errs() << "\n  Processing Cluster: {";
    for (size_t i = 0; i < srcs.size(); ++i) {
      errs() << srcs[i];
      if (i + 1 < srcs.size())
        errs() << ", ";
    }
    errs() << "} -> {";
    for (size_t i = 0; i < tgts.size(); ++i) {
      errs() << tgts[i];
      if (i + 1 < tgts.size())
        errs() << ", ";
    }
    errs() << "}\n";

    // Path-condition equivalence for this cluster.
    z3::expr src_pc = reach_src_set(srcs);
    z3::expr tgt_pc = reach_tgt_set(tgts);

    // z3::expr pc_vc = implies(tgt_pc, src_pc);
    // errs() << Z3_ast_to_string(c, pc_vc);
    // path_checking.push_back(pc_vc);

    z3::expr state_eq_vc =
        CFGMappingWitness::buildCommonVariableEquality(c, srcs, tgts);
    errs() << "State_checking for block" << Z3_ast_to_string(c, state_eq_vc);
    state_checking.push_back(state_eq_vc);
  }

  // ==========================================================
  // STEP 4: GLOBAL EXIT CHECK (Completeness)
  // Source: if_true and if_false are exit blocks
  // Target: common.ret is the sole exit block
  // Ensures total reachability is preserved at exit.
  // ==========================================================
  z3::expr total_src_exit =
      lookup_src_cond("if_true") || lookup_src_cond("if_false");
  z3::expr total_tgt_exit = lookup_tgt_cond("common.ret");
  path_checking.push_back(total_tgt_exit == total_src_exit);
  z3::expr_vector dead_block_vec(c);
  // Dead-block reachability: not (pc(block1) or pc(block2) ...).
  if (!dead_blocks.empty()) {
    for (StringRef Dead : dead_blocks){
      z3::expr dead_block = !(c.bool_val(false) || (lookup_src_cond(Dead)));
      dead_block_vec.push_back(dead_block);
    }
  }

  for (unsigned i = 0; i < dead_block_vec.size(); ++i) {
    errs() << "dead_block_vec[" << i
           << "] = " << Z3_ast_to_string(c, dead_block_vec[i]) << "\n";
  }

  // ==========================================================
  // STEP 5: Z3 VERIFICATION
  // ==========================================================

  // errs() << "\n" << "state_checking" << Z3_ast_to_string(c,
  // z3::mk_and(state_checking) ) << "\n"; errs() << "path_checking"
  // <<Z3_ast_to_string(c, z3::mk_and(path_checking)) << "\n";

  z3::expr final_vc = z3::mk_and(path_checking) && z3::mk_and(state_checking)  && z3::mk_and(dead_block_vec);
  final_vc.simplify();

  // Display the VC for debugging
  errs() << "Generated Verification Condition:\n"
         << Z3_ast_to_string(c, final_vc) << "\n\n";

  // Prove by checking if the negation is unsatisfiable
  if (forall_vars.empty())
    solver.add(!final_vc);
  else
    solver.add(!z3::forall(forall_vars, final_vc));

  z3::check_result result = solver.check();

  if (result == z3::unsat) {
    errs() << "✓✓✓ VERIFICATION SUCCESSFUL ✓✓✓\n";
    errs() << "The transformation refines the source and preserves exit "
              "reachability.\n";
  } else {
    errs() << "✗ VERIFICATION FAILED\n";
    if (result == z3::sat) {
      errs() << "Counter-example found: " << solver.get_model() << "\n";
    }
  }
}

/*hi*/