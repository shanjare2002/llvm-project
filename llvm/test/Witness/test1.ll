
define i32 @test_branch(i32 %x) {
entry:
  br label %merge
merge:
  %cmp = icmp slt i32 %x, 10
  br i1 %cmp, label %if_true, label %if_false

if_true:
  %x1 = add i32 %x, 1
  ret i32 12

if_false:
  %x12 = add i32 %x, 5
   ret i32 14
}
