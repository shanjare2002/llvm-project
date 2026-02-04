; ModuleID = 'test1.ll'
source_filename = "source_multi_blocks.c"

define i32 @source(i32 %x) {
entry:
  %x1 = add i32 %x, 1
  %y = alloca i32, align 4
  store i32 0, ptr %y, align 4
  %cmp1 = icmp sgt i32 %x1, 5
  br i1 %cmp1, label %if_a, label %if_b

if_a:                                             ; preds = %entry
  store i32 2, ptr %y, align 4
  br label %if_c

if_b:                                             ; preds = %entry
  store i32 3, ptr %y, align 4
  br label %if_c

if_c:                                             ; preds = %if_b, %if_a
  %y_val = load i32, ptr %y, align 4
  %cmp2 = icmp slt i32 %x, 0
  br i1 %cmp2, label %then_c, label %else_c

then_c:                                           ; preds = %if_c
  %y_inc1 = add i32 %y_val, 10
  store i32 %y_inc1, ptr %y, align 4
  br label %end

else_c:                                           ; preds = %if_c
  %y_inc2 = add i32 %y_val, 20
  store i32 %y_inc2, ptr %y, align 4
  br label %end

end:                                              ; preds = %else_c, %then_c
  %y_final = load i32, ptr %y, align 4
  %result = add i32 %y_final, %x1
  ret i32 %result
}
