; ModuleID = 'test1.ll'
source_filename = "test1.ll"

define i32 @test_branch(i32 %x) {
entry:
  %cmp = icmp slt i32 %x, 10
  br i1 %cmp, label %if_true, label %if_false

common.ret:                                       ; preds = %if_false, %if_true
  %common.ret.op = phi i32 [ 12, %if_true ], [ 14, %if_false ]
  ret i32 %common.ret.op

if_true:                                          ; preds = %entry
  %x1 = add i32 %x, 1
  br label %common.ret

if_false:                                         ; preds = %entry
  %x12 = add i32 %x, 5
  br label %common.ret
}
