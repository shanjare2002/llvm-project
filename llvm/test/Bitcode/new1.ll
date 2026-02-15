; ModuleID = 'test1.ll'
source_filename = "cfg_collapsible.c"

define i32 @source(i32 %x) {
entry:
  %x1 = add i32 %x, 1
  %y = alloca i32, align 4
  store i32 0, ptr %y, align 4
  store i32 2, ptr %y, align 4
  %y_val = load i32, ptr %y, align 4
  store i32 %y_val, ptr %y, align 4
  %y_final = load i32, ptr %y, align 4
  %result = add i32 %y_final, %x1
  ret i32 %result
}
