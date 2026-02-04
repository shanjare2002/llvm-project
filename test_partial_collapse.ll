; Example: SimplifyCFG will collapse blocks 'redundant_a' and 'redundant_b' 
; but keep 'real_work' and 'side_effect' separate
;
; Before optimization:
; - entry branches to three paths
; - redundant_a and redundant_b do the same thing (y = 2) -> will collapse
; - real_work does actual computation -> stays separate
; - side_effect has a call -> stays separate

define i32 @partial_collapse(i32 %x) {
entry:
  %cmp1 = icmp sgt i32 %x, 10
  br i1 %cmp1, label %real_work, label %check_redundant

check_redundant:
  %cmp2 = icmp sgt i32 %x, 5
  br i1 %cmp2, label %redundant_a, label %redundant_b

redundant_a:
  ; This block just sets y = 2 (redundant with redundant_b)
  %y_a = add i32 2, 0
  br label %merge_redundant

redundant_b:
  ; This block also just sets y = 2 (redundant with redundant_a)
  %y_b = add i32 2, 0
  br label %merge_redundant

merge_redundant:
  ; SimplifyCFG will collapse redundant_a and redundant_b into this block
  %y = phi i32 [ %y_a, %redundant_a ], [ %y_b, %redundant_b ]
  %temp = add i32 %y, %x
  br label %final

real_work:
  ; This block does meaningful computation, won't collapse
  %z = mul i32 %x, %x
  %w = add i32 %z, 3
  br label %side_effect

side_effect:
  ; This block has a side effect (call), won't collapse
  %v = phi i32 [ %w, %real_work ]
  ; Assume we have a function call here (side effect)
  %result1 = add i32 %v, 1
  br label %final

final:
  %result = phi i32 [ %temp, %merge_redundant ], [ %result1, %side_effect ]
  ret i32 %result
}
