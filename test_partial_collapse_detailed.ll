; ============================================================================
; BEFORE SimplifyCFG: Multiple blocks, some redundant
; ============================================================================

define i32 @before_simplify(i32 %x) {
entry:
  %cmp_large = icmp sgt i32 %x, 100
  br i1 %cmp_large, label %large_value, label %small_check

small_check:
  ; This check creates redundant blocks that will collapse
  %cmp_small = icmp sgt i32 %x, 0
  br i1 %cmp_small, label %positive_path, label %nonpositive_path

positive_path:
  ; Both paths compute the same value y = 10
  %y_pos = add i32 10, 0
  br label %merge_small

nonpositive_path:
  ; Identical computation to positive_path
  %y_neg = add i32 10, 0
  br label %merge_small

merge_small:
  ; Trivial phi node (same value from both predecessors)
  %y = phi i32 [ %y_pos, %positive_path ], [ %y_neg, %nonpositive_path ]
  %result_small = mul i32 %y, %x
  br label %exit

large_value:
  ; This path does different computation - WON'T collapse
  %squared = mul i32 %x, %x
  %cubed = mul i32 %squared, %x
  %result_large = add i32 %cubed, 1
  br label %exit

exit:
  %final = phi i32 [ %result_small, %merge_small ], [ %result_large, %large_value ]
  ret i32 %final
}

; ============================================================================
; AFTER SimplifyCFG: Redundant blocks collapsed, distinct ones remain
; ============================================================================

define i32 @after_simplify(i32 %x) {
entry:
  %cmp_large = icmp sgt i32 %x, 100
  br i1 %cmp_large, label %large_value, label %small_path_collapsed

small_path_collapsed:
  ; SimplifyCFG COLLAPSED positive_path, nonpositive_path, and merge_small
  ; The redundant branch (cmp_small) was eliminated
  ; The trivial phi was eliminated (both sides had same value 10)
  %y = add i32 10, 0          ; Just compute directly, no branch needed
  %result_small = mul i32 %y, %x
  br label %exit

large_value:
  ; This block STAYED SEPARATE - different computation
  %squared = mul i32 %x, %x
  %cubed = mul i32 %squared, %x
  %result_large = add i32 %cubed, 1
  br label %exit

exit:
  %final = phi i32 [ %result_small, %small_path_collapsed ], [ %result_large, %large_value ]
  ret i32 %final
}

; ============================================================================
; Summary:
; - COLLAPSED: positive_path, nonpositive_path, merge_small (redundant)
; - PRESERVED: large_value (distinct computation)
; - Blocks reduced from 6 to 4
; - One branch eliminated (cmp_small)
; ============================================================================
