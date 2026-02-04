define i32 @main() {
entry:
  ; Allocate space for an integer
  %x = alloca i32

  ; Dead store: store 42 into %x but never read it
  store i32 42, i32* %x

  ; Another variable that is actually used
  %y = alloca i32
  store i32 10, i32* %y
  %tmp = load i32, i32* %y

  ret i32 %tmp
}
