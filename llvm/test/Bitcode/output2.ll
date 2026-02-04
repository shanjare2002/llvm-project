; ModuleID = 'test1.ll'
source_filename = "test1.ll"

define i32 @main() {
entry:
  %i = add i32 0, 1
  br label %loop

loop:                                             ; preds = %body, %entry
  %cmp = icmp slt i32 %i, 3
  br i1 %cmp, label %body, label %after

body:                                             ; preds = %loop
  br label %loop

after:                                            ; preds = %loop
  ret i32 %i
}
