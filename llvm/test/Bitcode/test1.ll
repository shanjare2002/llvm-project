; ModuleID = 'cfg_collapsible'
source_filename = "cfg_collapsible.c"

define i32 @source(i32 %x) {
entry:
  ; x1 = x + 1
  %x1 = add i32 %x, 1

  ; allocate y
  %y = alloca i32
  store i32 0, i32* %y

  ; -------- first if --------
  %cmp1 = icmp sgt i32 %x1, 5
  br i1 %cmp1, label %if_a, label %if_b

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
