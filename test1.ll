define i32 @test(i32 %x) {
entry:
  %y = alloca i32
  br i1 undef, label %if_a, label %if_b

if_a:
  store i32 2, i32* %y
  br label %merge1

if_b:
  store i32 2, i32* %y
  br label %merge1

merge1:
  ; -------- second if --------
  %y_val = load i32, i32* %y
  %y_ret = add i32 %y_val, 1
  ret i32 %y_ret
}
