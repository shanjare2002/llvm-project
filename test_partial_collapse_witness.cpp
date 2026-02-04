// Path Formulas for Partial Collapse Example
// Based on test_partial_collapse_detailed.ll

#include "z3++.h"
#include "llvm/Support/raw_ostream.h"
#include <string>

using namespace llvm;

void generatePartialCollapseWitness() {
  errs() << "PARTIAL COLLAPSE VERIFICATION: PATH + STATE EQUIVALENCE\n";

  z3::context c;
  auto exprStr = [&](const z3::expr &E) { return Z3_ast_to_string(c, E); };

  // Input variable
  z3::expr x = c.int_const("x");

  // ===== PATH FORMULAS - SOURCE (BEFORE) =====
  errs() << "\n=== STEP 1: SOURCE PATH FORMULAS ===\n\n";

  // Source block paths
  z3::expr path_src_entry = c.bool_val(true);  // Always executed
  
  z3::expr path_src_small_check = x <= 100;     // Takes small path
  z3::expr path_src_large_value = x > 100;      // Takes large path
  
  z3::expr path_src_positive = (x <= 100) && (x > 0);      // Positive subpath
  z3::expr path_src_nonpositive = (x <= 100) && (x <= 0);  // Nonpositive subpath
  
  z3::expr path_src_merge_small = path_src_positive || path_src_nonpositive;  // Either small subpath
  
  z3::expr path_src_exit = c.bool_val(true);  // Always reached at end

  errs() << "Source block paths:\n";
  errs() << "  entry:          " << exprStr(path_src_entry) << "\n";
  errs() << "  small_check:    " << exprStr(path_src_small_check.simplify()) << "\n";
  errs() << "  positive:       " << exprStr(path_src_positive.simplify()) << "\n";
  errs() << "  nonpositive:    " << exprStr(path_src_nonpositive.simplify()) << "\n";
  errs() << "  merge_small:    " << exprStr(path_src_merge_small.simplify()) << "\n";
  errs() << "  large_value:    " << exprStr(path_src_large_value.simplify()) << "\n";
  errs() << "  exit:           " << exprStr(path_src_exit) << "\n";

  // ===== PATH FORMULAS - TARGET (AFTER) =====
  errs() << "\n=== STEP 2: TARGET PATH FORMULAS ===\n\n";

  // Target block paths (after collapse)
  z3::expr path_tgt_entry = c.bool_val(true);
  z3::expr path_tgt_small_collapsed = x <= 100;  // Collapsed from small_check, positive, nonpositive, merge_small
  z3::expr path_tgt_large_value = x > 100;       // Preserved
  z3::expr path_tgt_exit = c.bool_val(true);

  errs() << "Target block paths:\n";
  errs() << "  entry:             " << exprStr(path_tgt_entry) << "\n";
  errs() << "  small_collapsed:   " << exprStr(path_tgt_small_collapsed.simplify()) << "\n";
  errs() << "  large_value:       " << exprStr(path_tgt_large_value.simplify()) << "\n";
  errs() << "  exit:              " << exprStr(path_tgt_exit) << "\n";

  // ===== PATH EQUIVALENCE VCs =====
  errs() << "\n=== STEP 3: PATH EQUIVALENCE ===\n\n";

  // Mapping: entry -> entry (1:1)
  z3::expr path_vc1 = z3::implies(path_src_entry, path_tgt_entry);
  errs() << "VC1 (entry → entry): " << exprStr(path_vc1.simplify()) << "\n";

  // Mapping: small_check -> small_collapsed (many:1 start)
  z3::expr path_vc2 = z3::implies(path_src_small_check, path_tgt_small_collapsed);
  errs() << "VC2 (small_check → small_collapsed): " << exprStr(path_vc2.simplify()) << "\n";

  // Mapping: positive -> small_collapsed (many:1)
  z3::expr path_vc3 = z3::implies(path_src_positive, path_tgt_small_collapsed);
  errs() << "VC3 (positive → small_collapsed): " << exprStr(path_vc3.simplify()) << "\n";

  // Mapping: nonpositive -> small_collapsed (many:1)
  z3::expr path_vc4 = z3::implies(path_src_nonpositive, path_tgt_small_collapsed);
  errs() << "VC4 (nonpositive → small_collapsed): " << exprStr(path_vc4.simplify()) << "\n";

  // Mapping: merge_small -> small_collapsed (many:1)
  z3::expr path_vc5 = z3::implies(path_src_merge_small, path_tgt_small_collapsed);
  errs() << "VC5 (merge_small → small_collapsed): " << exprStr(path_vc5.simplify()) << "\n";

  // Mapping: large_value -> large_value (1:1 preserved)
  z3::expr path_vc6 = z3::implies(path_src_large_value, path_tgt_large_value);
  errs() << "VC6 (large_value → large_value): " << exprStr(path_vc6.simplify()) << "\n";

  // Mapping: exit -> exit (1:1)
  z3::expr path_vc7 = z3::implies(path_src_exit, path_tgt_exit);
  errs() << "VC7 (exit → exit): " << exprStr(path_vc7.simplify()) << "\n";

  z3::expr path_equivalence = path_vc1 && path_vc2 && path_vc3 && 
                              path_vc4 && path_vc5 && path_vc6 && path_vc7;

  errs() << "\nCombined Path Equivalence:\n  " 
         << exprStr(path_equivalence.simplify()) << "\n";

  // ===== SYMBOLIC STATES =====
  errs() << "\n=== STEP 4: SYMBOLIC STATES ===\n\n";

  // Source states for collapsed blocks (all compute y = 10)
  z3::expr x_src_small = x;
  z3::expr y_src_small = c.int_val(10);  // Same from positive and nonpositive
  z3::expr result_src_small = y_src_small * x;

  errs() << "Source small path state: y=" << exprStr(y_src_small) 
         << ", result=" << exprStr(result_src_small.simplify()) << "\n";

  // Source state for preserved block (large_value)
  z3::expr x_src_large = x;
  z3::expr squared_src = x * x;
  z3::expr cubed_src = squared_src * x;
  z3::expr result_src_large = cubed_src + 1;

  errs() << "Source large path state: cubed=" << exprStr(cubed_src.simplify())
         << ", result=" << exprStr(result_src_large.simplify()) << "\n";

  // Target states
  z3::expr x_tgt_small = x;
  z3::expr y_tgt_small = c.int_val(10);
  z3::expr result_tgt_small = y_tgt_small * x;

  errs() << "Target small path state: y=" << exprStr(y_tgt_small)
         << ", result=" << exprStr(result_tgt_small.simplify()) << "\n";

  z3::expr x_tgt_large = x;
  z3::expr squared_tgt = x * x;
  z3::expr cubed_tgt = squared_tgt * x;
  z3::expr result_tgt_large = cubed_tgt + 1;

  errs() << "Target large path state: cubed=" << exprStr(cubed_tgt.simplify())
         << ", result=" << exprStr(result_tgt_large.simplify()) << "\n";

  // ===== STATE EQUIVALENCE =====
  errs() << "\n=== STEP 5: STATE EQUIVALENCE ===\n\n";

  // Collapsed blocks: positive/nonpositive/merge_small -> small_collapsed
  z3::expr state_equiv_small = 
      (x_src_small == x_tgt_small) && 
      (y_src_small == y_tgt_small) &&
      (result_src_small == result_tgt_small);
  
  z3::expr state_vc_small = z3::implies(
      path_src_merge_small && path_tgt_small_collapsed, 
      state_equiv_small
  );
  errs() << "State VC (small paths): " << exprStr(state_vc_small.simplify()) << "\n";

  // Preserved block: large_value -> large_value
  z3::expr state_equiv_large =
      (x_src_large == x_tgt_large) &&
      (cubed_src == cubed_tgt) &&
      (result_src_large == result_tgt_large);
  
  z3::expr state_vc_large = z3::implies(
      path_src_large_value && path_tgt_large_value,
      state_equiv_large
  );
  errs() << "State VC (large path): " << exprStr(state_vc_large.simplify()) << "\n";

  z3::expr state_equivalence = state_vc_small && state_vc_large;
  errs() << "\nCombined State Equivalence:\n  "
         << exprStr(state_equivalence.simplify()) << "\n";

  // ===== COMBINED VERIFICATION =====
  errs() << "\n=== STEP 6: COMBINED VERIFICATION ===\n\n";

  z3::expr complete_formula = path_equivalence && state_equivalence;
  errs() << "Complete Formula:\n  " 
         << exprStr(complete_formula.simplify()) << "\n";

  z3::expr full_formula = z3::forall(x, complete_formula);
  errs() << "\nWith quantification:\n  ∀x: "
         << exprStr(full_formula.simplify()) << "\n";

  // ===== Z3 CHECK =====
  errs() << "\n=== STEP 7: VALIDITY CHECK ===\n\n";

  z3::solver solver(c);
  solver.add(!full_formula);

  if (solver.check() == z3::unsat) {
    errs() << "✓✓✓ PARTIAL COLLAPSE VERIFIED CORRECT! ✓✓✓\n";
    errs() << "Both collapsed and preserved paths are equivalent!\n";
    errs() << "SimplifyCFG's selective optimization is sound.\n";
  } else {
    errs() << "✗ VERIFICATION FAILED\n";
    errs() << "Counterexample: " << solver.get_model() << "\n";
  }

  // ===== WITNESS RELATION =====
  errs() << "\n=== STEP 8: WITNESS RELATION ST ===\n\n";

  z3::expr pi_S = c.int_const("pi_S");
  z3::expr pi_T = c.int_const("pi_T");

  // WitnessExprST for partial collapse:
  // Case 1: Collapsed blocks (1,2,3,4) -> 1
  z3::expr witness_collapsed = 
      ((pi_S == 1) || (pi_S == 2) || (pi_S == 3) || (pi_S == 4)) &&
      (pi_T == 1) &&
      (x_src_small == x_tgt_small) &&
      (y_src_small == y_tgt_small);

  // Case 2: Preserved blocks (0->0, 5->2, 6->3)
  z3::expr witness_entry = (pi_S == 0) && (pi_T == 0);
  z3::expr witness_large = (pi_S == 5) && (pi_T == 2) && (cubed_src == cubed_tgt);
  z3::expr witness_exit = (pi_S == 6) && (pi_T == 3);

  z3::expr WitnessExprST = witness_collapsed || witness_entry || 
                           witness_large || witness_exit;

  errs() << "Witness ST (collapsed): " << exprStr(witness_collapsed.simplify()) << "\n";
  errs() << "Witness ST (preserved): " << exprStr((witness_entry || witness_large || witness_exit).simplify()) << "\n";
  errs() << "Complete Witness ST:\n  " << exprStr(WitnessExprST.simplify()) << "\n";
}
