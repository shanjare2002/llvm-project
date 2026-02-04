; ModuleID = 'dead_code.ll'
source_filename = "dead_code.ll"

define i32 @main() {
entry:
  %y = alloca i32, align 4
  store i32 10, ptr %y, align 4
  %tmp = load i32, ptr %y, align 4
  ret i32 %tmp
}
