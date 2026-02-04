; ModuleID = 'test1.ll'
source_filename = "test1.ll"

define i32 @main() {


dead_block:
  %a = add i32 5, 10
  %b = mul i32 %a, 2
  ret i32 %b
  
entry:
  %x = add i32 0, 1
  %y = add i32 %x, 1
  ret i32 %y


}
