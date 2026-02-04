; Example with two blocks merged, then a separate final block
define i32 @test(i32 %x) {
entry:
  %cmp = icmp sgt i32 %x, 5
  br i1 %cmp, label %then.block, label %else.block

then.block:
  br label %merge.block

else.block:
  br label %merge.block

merge.block:
  %y = add i32 %x, 10
  br label %final.block

final.block:
  %z = mul i32 %y, 2
  ret i32 %z
}
