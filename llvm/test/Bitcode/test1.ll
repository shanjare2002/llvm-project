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
deadblock: 
 %u = add i32 0, 1
 br label %merge1
if_b:
  store i32 2, i32* %y
  br label %merge1

merge1:
  ; -------- second if --------
  %y_val = load i32, i32* %y
  %cmp2 = icmp slt i32 %x, 0
  br i1 %cmp2, label %if_c, label %if_d

if_c:
  store i32 %y_val, i32* %y
  br label %merge2

if_d:
  store i32 %y_val, i32* %y
  br label %merge2

merge2:
  %y_final = load i32, i32* %y
  %result = add i32 %y_final, %x1
  ret i32 %result
}
