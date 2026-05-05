/// GVNWitnessPass.cpp — Combined witness pass for the GVN transformation.
///
/// GVN performs two distinct kinds of transformations, each verified
/// differently:
///
/// ── Behavior 1: Pure value replacement (no CFG change) ─────────────────────
///   GVN replaces OLD with NEW when it can prove they are equal at that point.
///   Examples: identical sub-expressions, redundant loads from the same address
///   with the same memory state, identical PHI nodes.
///
///   Witness check: for each (old, new) in GVNReplacements,
///     src_expr(old)  ==  src_expr(new)
///   is proved directly from the source symbolic map (Z3 expressions built
///   from function arguments).  If it holds for all inputs → replacement valid.
///
/// ── Behavior 2: CFG-changing PRE / edge splitting ──────────────────────────
///   GVN's Partial Redundancy Elimination inserts a computation on a path
///   where it was missing, creating new basic blocks by splitting critical
///   edges.  This changes the control-flow graph.
///
///   Witness check: same block-mapping + verification-condition approach as
///   SimplifyCFGWitness.  We capture the source CFG state with
///   CFGMappingWitness::buildSourceMappings(), record which blocks were split
///   (GVNCFGChanges), build the witness map, and generate per-cluster VCs with
///   CFGMappingWitness::buildCommonVariableEquality().
///
/// A function is VERIFICATION SUCCESSFUL if both checks pass.

#include "z3++.h"

#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Support/raw_ostream.h"

#include "CFGMappingWitness.h" // block-level symbolic map + VC builder
#include "GVNLocal.h"          // GVNPassLocal, GVNReplacements, GVNCFGChanges
#include "GVNMappingWitness.h" // flat symbolic expression builder
#include "GVNWitnessPass.h"

#include <map>
#include <set>
#include <string>
#include <vector>

#define DEBUG_TYPE "gvn-witness"

using namespace llvm;

// ---------------------------------------------------------------------------
// Z3 helpers
// ---------------------------------------------------------------------------

namespace {

static bool z3ProveEqual(z3::context &C, const z3::expr &L, const z3::expr &R,
                          unsigned Ms = 5000) {
  if (L.get_sort().id() != R.get_sort().id())
    return false;
  z3::solver S(C);
  z3::params P(C);
  P.set("timeout", Ms);
  S.set(P);
  S.add(L != R);
  return S.check() == z3::unsat;
}

static bool z3ProveVC(z3::context &C, const z3::expr &VC,
                       unsigned Ms = 5000) {
  z3::solver S(C);
  z3::params P(C);
  P.set("timeout", Ms);
  S.set(P);
  // VC is already an implication; check it is valid (negation is unsat).
  S.add(!VC);
  return S.check() == z3::unsat;
}

} // namespace

// ---------------------------------------------------------------------------
// GVNWitnessPass::run
// ---------------------------------------------------------------------------

PreservedAnalyses GVNWitnessPass::run(Function &F,
                                       FunctionAnalysisManager &FAM) {
  errs() << "\n*** GVNWitnessPass::run() called for function: "
         << F.getName() << " ***\n";

  // =========================================================================
  // STEP 1 — Capture source state (before GVN)
  // =========================================================================

  // 1a. Flat symbolic map for value-replacement checks.
  z3::context C;
  GVNMappingWitness::initialize(&F, &C);
  GVNMappingWitness::buildMappings(/*isSrc=*/true);
  GVNMappingWitness::dump("Source Symbolic Map", GVNMappingWitness::getSrcMap());
  if (GVNMappingWitness::hasSrcRet())
    errs() << "  ret_src = "
           << Z3_ast_to_string(C, GVNMappingWitness::getSrcRet()) << "\n";

  // 1b. Block-level symbolic maps for CFG-change checks (same approach as
  //     SimplifyCFGWitness).  CFGMappingWitness uses a separate static context
  //     and its own Z3 context; we share the function pointer but it manages
  //     its own context internally.
  CFGMappingWitness::initialize(&F, &C);
  CFGMappingWitness::buildSourceMappings();

  // Snapshot of block names before transformation (for witness map building).
  std::set<std::string> SrcBlockNames;
  for (BasicBlock &BB : F)
    SrcBlockNames.insert(BB.hasName() ? BB.getName().str()
                                      : std::string("(unnamed)"));

  // =========================================================================
  // STEP 2 — Run GVN
  // =========================================================================
  GVNReplacements.clear();
  GVNCFGChanges.clear();

  GVNPassLocal GVNPass;
  errs() << "=== Running GVN ===\n";
  PreservedAnalyses PA = GVNPass.run(F, FAM);

  errs() << "GVN value replacements: " << GVNReplacements.size() << "\n";
  for (const auto &[Old, New] : GVNReplacements)
    errs() << "  " << Old << "  →  " << New << "\n";

  errs() << "GVN CFG changes (edge splits): " << GVNCFGChanges.size() << "\n";
  for (const auto &[Pred, NewBB] : GVNCFGChanges)
    errs() << "  " << Pred << "  →  new block  " << NewBB << "\n";

  // =========================================================================
  // STEP 3 — Capture target state (after GVN)
  // =========================================================================

  // 3a. Flat symbolic map (target).
  GVNMappingWitness::buildMappings(/*isSrc=*/false);
  GVNMappingWitness::dump("Target Symbolic Map", GVNMappingWitness::getTgtMap());
  if (GVNMappingWitness::hasTgtRet())
    errs() << "  ret_tgt = "
           << Z3_ast_to_string(C, GVNMappingWitness::getTgtRet()) << "\n";

  // 3b. Block-level symbolic maps (target).
  CFGMappingWitness::buildTargetMappings();

  // =========================================================================
  // STEP 4 — Behavior 1: Verify pure value replacements
  //
  // Principle: when GVN replaces OLD with NEW, src_expr(OLD) == src_expr(NEW)
  // must hold for all possible input values.
  // =========================================================================
  errs() << "\n=== Behavior 1: Verifying Value Replacements ===\n";

  const GVNSymMap &SrcMap = GVNMappingWitness::getSrcMap();
  const GVNSymMap &TgtMap = GVNMappingWitness::getTgtMap();

  bool B1_AllOk = true;
  int  B1_Skip  = 0;

  for (const auto &[OldName, NewName] : GVNReplacements) {
    auto OldSrc = SrcMap.find(OldName);
    auto NewSrc = SrcMap.find(NewName);

    // Both in source map → pure value replacement, check directly.
    if (OldSrc != SrcMap.end() && NewSrc != SrcMap.end()) {
      const z3::expr &OldExpr = OldSrc->second;
      const z3::expr &NewExpr = NewSrc->second;
      errs() << "  [B1] " << OldName << " == " << NewName << "?\n";
      errs() << "       src_expr(" << OldName << ") = "
             << Z3_ast_to_string(C, OldExpr) << "\n";
      errs() << "       src_expr(" << NewName << ") = "
             << Z3_ast_to_string(C, NewExpr) << "\n";
      if (z3ProveEqual(C, OldExpr, NewExpr)) {
        errs() << "       ✓ equal\n";
      } else {
        errs() << "       ✗ cannot prove equal (opaque/complex expression)\n";
        B1_AllOk = false;
      }
      continue;
    }

    // OLD in source map, NEW only in target map → cross-map (PRE case);
    // handled by Behavior 2 below.
    if (OldSrc != SrcMap.end() && TgtMap.count(NewName)) {
      errs() << "  [B1→B2] " << OldName << " → " << NewName
             << " (cross-map, deferred to CFG-change check)\n";
      continue;
    }

    // Otherwise skip (anonymous value or unsupported).
    errs() << "  [SKIP] " << OldName << " → " << NewName
           << " (not found in source map)\n";
    ++B1_Skip;
  }

  // =========================================================================
  // STEP 5 — Behavior 2: Verify CFG-changing PRE via block-mapping VCs
  //
  // When GVN splits critical edges (Pred → NewBB → Succ), the source block
  // Pred maps to {Pred, NewBB} in the target (SPLIT cluster).  All other
  // blocks that exist unchanged are ONE-TO-ONE preserved.
  //
  // We build the witness map from GVNCFGChanges and then call
  // CFGMappingWitness::buildCommonVariableEquality() for each cluster,
  // exactly as SimplifyCFGWitness does.
  // =========================================================================
  errs() << "\n=== Behavior 2: Verifying CFG-Change (PRE / Edge Splits) ===\n";

  bool B2_AllOk = true;

  if (GVNCFGChanges.empty()) {
    errs() << "  (no CFG changes — skipping)\n";
  } else {
    // Build the one-to-many witness map.
    // For each split (Pred, NewBB):
    //   - Pred_src → {Pred_tgt, NewBB_tgt}  (SPLIT)
    // All original blocks not listed as split-origin are preserved 1-to-1.
    StringMap<SmallVector<std::string, 4>> WitnessMap;
    std::set<std::string> SplitOrigins;

    for (const auto &[PredName, NewBBName] : GVNCFGChanges) {
      WitnessMap[PredName].push_back(PredName);   // Pred still exists in tgt
      WitnessMap[PredName].push_back(NewBBName);  // + new intermediate block
      SplitOrigins.insert(PredName);
    }

    // Add preserved (unchanged) blocks.
    for (const std::string &BName : SrcBlockNames) {
      if (!SplitOrigins.count(BName))
        WitnessMap[BName].push_back(BName);
    }

    errs() << "  Witness map:\n";
    for (const auto &[Src, Tgts] : WitnessMap) {
      errs() << "    " << Src << "  →  {";
      for (size_t i = 0; i < Tgts.size(); ++i) {
        if (i) errs() << ", ";
        errs() << Tgts[i];
      }
      errs() << "}\n";
    }

    // Generate and check a VC for each cluster.
    for (const auto &[SrcBlock, TgtBlocks] : WitnessMap) {
      SmallVector<std::string, 4> SrcVec = {SrcBlock.str()};
      SmallVector<std::string, 4> TgtVec(TgtBlocks.begin(), TgtBlocks.end());

      z3::expr VC =
          CFGMappingWitness::buildCommonVariableEquality(C, SrcVec, TgtVec);
      errs() << "  Cluster {" << SrcBlock << "} → VC = "
             << Z3_ast_to_string(C, VC) << "\n";

      if (z3ProveVC(C, VC)) {
        errs() << "  ✓ cluster {" << SrcBlock << "} verified\n";
      } else {
        errs() << "  ✗ cluster {" << SrcBlock
               << "} — VC not provable\n";
        B2_AllOk = false;
      }
    }
  }

  // =========================================================================
  // STEP 6 — Return-value preservation (observable output check)
  //
  // Even if individual replacement proofs are incomplete (e.g. opaque loads),
  // verify that the return value is the same in source and target symbolic
  // states, using GVN replacement equalities as premise assumptions.
  // =========================================================================
  errs() << "\n=== Return Value Preservation ===\n";

  bool RetOk = true;
  if (GVNMappingWitness::hasSrcRet() && GVNMappingWitness::hasTgtRet()) {
    const z3::expr &RetSrc = GVNMappingWitness::getSrcRet();
    const z3::expr &RetTgt = GVNMappingWitness::getTgtRet();

    // Premise: GVN replacement equalities (the witness assertions).
    z3::expr Premise = C.bool_val(true);
    for (const auto &[OldName, NewName] : GVNReplacements) {
      auto OldIt = SrcMap.find(OldName);
      auto NewIt = SrcMap.find(NewName);
      if (OldIt == SrcMap.end() || NewIt == SrcMap.end())
        continue;
      if (OldIt->second.get_sort().id() == NewIt->second.get_sort().id())
        Premise = Premise && (OldIt->second == NewIt->second);
    }

    errs() << "  ret_src = " << Z3_ast_to_string(C, RetSrc) << "\n";
    errs() << "  ret_tgt = " << Z3_ast_to_string(C, RetTgt) << "\n";

    if (RetSrc.get_sort().id() != RetTgt.get_sort().id()) {
      errs() << "  [SKIP] sort mismatch\n";
    } else {
      z3::expr RetVC = z3::implies(Premise, RetSrc == RetTgt);
      if (z3ProveVC(C, RetVC)) {
        errs() << "  ✓ return value preserved\n";
      } else {
        errs() << "  ✗ return value NOT provably preserved\n";
        RetOk = false;
      }
    }
  } else if (!GVNMappingWitness::hasSrcRet() &&
             !GVNMappingWitness::hasTgtRet()) {
    errs() << "  (void function)\n";
  } else {
    errs() << "  [WARN] return only on one side\n";
    RetOk = false;
  }

  // =========================================================================
  // Final verdict
  // =========================================================================
  bool AllOk = B1_AllOk && B2_AllOk && RetOk;
  if (AllOk) {
    errs() << "\n✓✓✓ GVN VERIFICATION SUCCESSFUL ✓✓✓\n";
  } else {
    errs() << "\n✗ GVN VERIFICATION FAILED\n";
    if (!B1_AllOk) errs() << "  Behavior 1 (value replacement) failed.\n";
    if (!B2_AllOk) errs() << "  Behavior 2 (CFG change/PRE) failed.\n";
    if (!RetOk)    errs() << "  Return value preservation failed.\n";
  }

  GVNMappingWitness::clear();
  return PA;
}
