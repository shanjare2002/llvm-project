; Simple function with branches that SimplifyCFG can optimize
define i32 @test(i32 %x, i32 %y) {
entry:
  %cmp = icmp sgt i32 %x, 0
  br i1 %cmp, label %if.then, label %if.else

if.then:
  %add1 = add i32 %x, 1
  br label %merge

if.else:
  %add2 = add i32 %x, 1
  br label %merge

merge:
  %result = phi i32 [ %add1, %if.then ], [ %add2, %if.else ]
  %final = add i32 %result, %y
  ret i32 %final
}
