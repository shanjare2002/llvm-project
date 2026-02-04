; ModuleID = 'test1.ll'
source_filename = "test1.ll"

define i32 @main() {
entry:
  %x = add i32 0, 1
  %y = add i32 %x, 1
  ret i32 %y
}
