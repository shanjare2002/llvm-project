#include "llvm/Support/raw_ostream.h"
#include <string>
#include <z3++.h>

using namespace std;
using namespace z3;
using namespace llvm;

int main() {
  context c;

  errs() << "=" << string(68, '=') << "\n";
  errs() << "COMPLETE VERIFICATION: PATH + STATE EQUIVALENCE\n";
  errs() << "=" << string(68, '=') << "\n";

  // Input variable
  expr x = c.int_const("x");

  // ===== PATH FORMULAS =====
  errs() << "\n=== STEP 1: PATH EQUIVALENCE ===\n\n";

  expr path_src_a = (x + 1) > 5;
  expr path_src_b = !((x + 1) > 5);
  expr path_src_end = path_src_a || path_src_b;
  expr path_tgt_entry = c.bool_val(true);

  errs() << "Source paths:\n";
  errs() << "  a:   " << simplify(path_src_a) << " = (x > 4)\n";
  errs() << "  b:   " << simplify(path_src_b) << " = (x ≤ 4)\n";
  errs() << "  end: " << simplify(path_src_end) << "\n";
  errs() << "\nTarget paths:\n";
  errs() << "  entry: " << path_tgt_entry << "\n";

  // Path equivalence VCs
  expr path_vc1 = implies(path_src_a, path_tgt_entry);
  expr path_vc2 = implies(path_src_b, path_tgt_entry);
  expr path_vc3 = implies(path_src_end, path_tgt_entry);

  expr path_equivalence = path_vc1 && path_vc2 && path_vc3;

  errs() << "\nPath Equivalence Formula:\n";
  errs() << "  " << simplify(path_equivalence) << "\n";

  // ===== SYMBOLIC STATES =====
  errs() << "\n=== STEP 2: COMPUTE SYMBOLIC STATES ===\n\n";

  // Source states
  errs() << "Source.a state (when x > 4):\n";
  expr x_src_a = x;
  expr x1_src_a = x + 1;
  expr y_src_a = c.int_val(2);
  errs() << "  x:   " << x_src_a << "\n";
  errs() << "  x1:  " << x1_src_a << "\n";
  errs() << "  y_a: " << y_src_a << "\n";

  errs() << "\nSource.b state (when x ≤ 4):\n";
  expr x_src_b = x;
  expr x1_src_b = x + 1;
  expr y_src_b = c.int_val(2);
  errs() << "  x:   " << x_src_b << "\n";
  errs() << "  x1:  " << x1_src_b << "\n";
  errs() << "  y_b: " << y_src_b << "\n";

  errs() << "\nSource.end state:\n";
  expr x_src_end = x;
  expr x1_src_end = x + 1;
  expr y_src_end = c.int_val(2);
  expr result_src_end = y_src_end + x1_src_end;
  errs() << "  x:      " << x_src_end << "\n";
  errs() << "  x1:     " << x1_src_end << "\n";
  errs() << "  y:      " << y_src_end << "\n";
  errs() << "  result: " << simplify(result_src_end) << "\n";

  // Target state
  errs() << "\nTarget.entry state:\n";
  expr x_tgt = x;
  expr x1_tgt = x + 1;
  expr y_tgt = c.int_val(2);
  expr result_tgt = y_tgt + x1_tgt;
  errs() << "  x:      " << x_tgt << "\n";
  errs() << "  x1:     " << x1_tgt << "\n";
  errs() << "  y:      " << y_tgt << "\n";
  errs() << "  result: " << simplify(result_tgt) << "\n";

  // ===== STATE EQUIVALENCE =====
  errs() << "\n=== STEP 3: STATE EQUIVALENCE ===\n\n";

  // Equivalence Point 1: (a, entry)
  errs() << "Equivalence Point 1: (Source.a, Target.entry)\n";
  expr state_equiv_a =
      x_src_a == x_tgt && x1_src_a == x1_tgt && y_src_a == y_tgt;
  errs() << "  State match: " << simplify(state_equiv_a) << "\n";

  expr state_vc1 = implies(path_src_a && path_tgt_entry, state_equiv_a);
  errs() << "  VC: " << simplify(state_vc1) << "\n";

  // Equivalence Point 2: (b, entry)
  errs() << "\nEquivalence Point 2: (Source.b, Target.entry)\n";
  expr state_equiv_b =
      x_src_b == x_tgt && x1_src_b == x1_tgt && y_src_b == y_tgt;
  errs() << "  State match: " << simplify(state_equiv_b) << "\n";

  expr state_vc2 = implies(path_src_b && path_tgt_entry, state_equiv_b);
  errs() << "  VC: " << simplify(state_vc2) << "\n";

  // Equivalence Point 3: (end, entry) - RETURN VALUES
  errs() << "\nEquivalence Point 3: (Source.end, Target.entry)\n";
  expr state_equiv_end = x_src_end == x_tgt && x1_src_end == x1_tgt &&
                         y_src_end == y_tgt && result_src_end == result_tgt;
  errs() << "  State match: " << simplify(state_equiv_end) << "\n";

  expr state_vc3 = implies(path_src_end && path_tgt_entry, state_equiv_end);
  errs() << "  VC: " << simplify(state_vc3) << "\n";

  // Combine all state VCs
  expr state_equivalence = state_vc1 && state_vc2 && state_vc3;

  errs() << "\nState Equivalence Formula:\n";
  errs() << "  " << simplify(state_equivalence) << "\n";

  // ===== COMBINED FORMULA =====
  errs() << "\n=== STEP 4: COMBINED VERIFICATION ===\n\n";

  expr complete_formula = path_equivalence && state_equivalence;

  errs() << "Complete Formula (Path + State):\n";
  errs() << "  " << simplify(complete_formula) << "\n";

  // Add quantification
  expr full_formula = forall(x, complete_formula);

  errs() << "\nWith quantification:\n";
  errs() << "  ∀ x: " << simplify(path_equivalence && state_equivalence)
         << "\n";

  // ===== Z3 CHECK =====
  errs() << "\n=== STEP 5: VALIDITY CHECK ===\n\n";

  solver sol(c);
  sol.add(!full_formula);

  check_result result = sol.check();

  if (result == unsat) {
    errs() << "✓✓✓ TRANSFORMATION VERIFIED CORRECT! ✓✓✓\n";
    errs() << "\nBoth path equivalence AND state equivalence hold!\n";
    errs() << "SimplifyCFG's optimization is sound.\n";
  } else {
    errs() << "✗ VERIFICATION FAILED\n";
    errs() << "Counterexample: " << sol.get_model() << "\n";
  }

  // ===== HARDCODED WITNESS DATA =====
  errs() << "\n=== STEP 6: WITNESS GENERATION ===\n\n";

  // Program counters for witness
  expr pi_S = c.int_const("pi_S");
  expr pi_T = c.int_const("pi_T");

  // Hardcoded witness relations
  errs() << "Block Mapping (Source → Target):\n";
  errs() << "  Block a → entry\n";
  errs() << "  Block b → entry\n";
  errs() << "  Block end → entry\n";

  // WitnessExprST: Source to Target equivalence
  expr WitnessExprST = c.bool_val(false);
  expr FinalST = (pi_S == -1) && (pi_T == -1) && (x_src_a == x_tgt) &&
                 (x1_src_a == x1_tgt) && (y_src_a == y_tgt);
  WitnessExprST = WitnessExprST || FinalST;

  errs() << "\nWitness Relations (Hardcoded):\n";
  errs() << "  ST: " << simplify(WitnessExprST) << "\n";

  // ===== WITNESS VERIFICATION =====
  errs() << "\n=== STEP 7: WITNESS VERIFICATION ===\n\n";

  solver witness_solver(c);
  witness_solver.add(WitnessExprST);

  check_result witness_check = witness_solver.check();

  if (witness_check == sat) {
    errs() << "✓ Witness relation ST is satisfiable\n";
    errs() << "Witness model:\n";
    model m = witness_solver.get_model();
    errs() << "  " << m << "\n";
  } else {
    errs() << "✗ Witness relation ST unsatisfiable\n";
  }

  errs() << "\n" << string(68, '=') << "\n";
  errs() << "VERIFICATION COMPLETE\n";
  errs() << string(68, '=') << "\n";

  return 0;
}
