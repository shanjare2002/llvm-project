// Block Mapping for Partial SimplifyCFG Collapse
// Source (Before) → Target (After)
//
// This shows which source blocks map to which target blocks
// after SimplifyCFG optimization

/*
BEFORE SimplifyCFG (Source):
  Block 0: entry
  Block 1: small_check
  Block 2: positive_path
  Block 3: nonpositive_path
  Block 4: merge_small
  Block 5: large_value
  Block 6: exit

AFTER SimplifyCFG (Target):
  Block 0: entry
  Block 1: small_path_collapsed
  Block 2: large_value
  Block 3: exit

WITNESS MAPPING (Source → Target):
  entry            (0) → entry                 (0)  [1:1 - preserved]
  small_check      (1) → small_path_collapsed  (1)  [many:1 - collapsed]
  positive_path    (2) → small_path_collapsed  (1)  [many:1 - collapsed]
  nonpositive_path (3) → small_path_collapsed  (1)  [many:1 - collapsed]
  merge_small      (4) → small_path_collapsed  (1)  [many:1 - collapsed]
  large_value      (5) → large_value           (2)  [1:1 - preserved]
  exit             (6) → exit                  (3)  [1:1 - preserved]

KEY OBSERVATIONS:
1. COLLAPSED: Blocks 1,2,3,4 all map to single target block 1
   - These blocks had redundant computation (y = 10 from both paths)
   - The branch condition (x > 0) was eliminated as useless

2. PRESERVED: Blocks 5 and 6 maintain 1:1 mapping
   - large_value does distinct computation (cubing)
   - exit is needed for control flow merge

WITNESS RELATION ST:
  For collapsed blocks (1,2,3,4 → 1):
    pi_S ∈ {1,2,3,4} ∧ pi_T = 1 ∧ 
    (state_vars equal: x_S = x_T, y_S = y_T = 10)

  For preserved blocks (0→0, 5→2, 6→3):
    pi_S = i ∧ pi_T = corresponding_target ∧
    (all state_vars equal)

STUTTERING:
  - Source blocks 1,2,3,4 are stuttering equivalents (same effect)
  - Target block 1 represents the collapsed state
  - Source blocks 5,6 have no stuttering (unique computations)
*/

// Programmatic representation:
struct BlockMapping {
    int source_block;
    int target_block;
    const char* source_name;
    const char* target_name;
    const char* type;
};

struct BlockMapping witness_map[] = {
    {0, 0, "entry",            "entry",               "1:1 preserved"},
    {1, 1, "small_check",      "small_path_collapsed", "many:1 collapsed"},
    {2, 1, "positive_path",    "small_path_collapsed", "many:1 collapsed"},
    {3, 1, "nonpositive_path", "small_path_collapsed", "many:1 collapsed"},
    {4, 1, "merge_small",      "small_path_collapsed", "many:1 collapsed"},
    {5, 2, "large_value",      "large_value",         "1:1 preserved"},
    {6, 3, "exit",             "exit",                "1:1 preserved"}
};

// Z3 Witness Formula for this mapping:
/*
WitnessExprST = 
    // Case 1: Collapsed blocks
    ((pi_S == 1 || pi_S == 2 || pi_S == 3 || pi_S == 4) && pi_T == 1 &&
     x_S == x_T && y_S == 10 && y_T == 10) ||
    
    // Case 2: Preserved blocks
    (pi_S == 0 && pi_T == 0 && x_S == x_T) ||
    (pi_S == 5 && pi_T == 2 && x_S == x_T && cubed_S == cubed_T) ||
    (pi_S == 6 && pi_T == 3 && final_S == final_T)
*/
