; ModuleID = 'test1.ll'
source_filename = "test1.ll"

define i32 @test(i32 %x) {
entry:
  %y = alloca i32, align 4
  store i32 2, ptr %y, align 4
  %y_val = load i32, ptr %y, align 4
  %y_ret = add i32 %y_val, 1
  ret i32 %y_ret
}
