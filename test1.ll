define i32 @source(i32 %x) {
entry:
  %x1 = add i32 %x, 1
  br label %end

if_a:
  %x2 = add i32 %x, 1
  %y1 = add i32 %x2, 1
  br label %end

end:
  ret i32 %x1
}
