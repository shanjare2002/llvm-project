; ModuleID = 'test1.ll'
source_filename = "cfg_collapsible.c"

define i32 @source(i32 %x) {
entry:
  %x1 = add i32 %x, 1
  %y = alloca i32, align 4
  store i32 0, ptr %y, align 4
  %cmp1 = icmp sgt i32 %x1, 5
  br i1 %cmp1, label %if_a, label %if_b

if_a:                                             ; preds = %entry
  store i32 2, ptr %y, align 4
  br label %merge1

deadblock:                                        ; No predecessors!
  br label %merge1

if_b:                                             ; preds = %entry
  store i32 2, ptr %y, align 4
  br label %merge1

merge1:                                           ; preds = %if_b, %deadblock, %if_a
  %y_val = load i32, ptr %y, align 4
  %cmp2 = icmp slt i32 %x, 0
  br i1 %cmp2, label %if_c, label %if_d

if_c:                                             ; preds = %merge1
  store i32 %y_val, ptr %y, align 4
  br label %merge2

if_d:                                             ; preds = %merge1
  store i32 %y_val, ptr %y, align 4
  br label %merge2

merge2:                                           ; preds = %if_d, %if_c
  %y_final = load i32, ptr %y, align 4
  %result = add i32 %y_final, %x1
  ret i32 %result
}
