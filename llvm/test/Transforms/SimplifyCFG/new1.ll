; ModuleID = 'hoist-common-code.ll'
source_filename = "hoist-common-code.ll"

declare void @bar(i32)

define void @test(i1 %P, ptr %Q) {
  br i1 %P, label %T, label %F

common.ret:                                       ; preds = %F, %T
  ret void

T:                                                ; preds = %0
  store i32 1, ptr %Q, align 4
  %A = load i32, ptr %Q, align 4
  call void @bar(i32 %A)
  br label %common.ret

F:                                                ; preds = %0
  store i32 1, ptr %Q, align 4
  %B = load i32, ptr %Q, align 4
  call void @bar(i32 %B)
  br label %common.ret
}

define void @test_switch(i64 %i, ptr %Q) {
  switch i64 %i, label %bb0 [
    i64 1, label %bb1
    i64 2, label %bb2
  ]

common.ret:                                       ; preds = %bb2, %bb1, %bb0
  ret void

bb0:                                              ; preds = %0
  store i32 1, ptr %Q, align 4
  %A = load i32, ptr %Q, align 4
  call void @bar(i32 %A)
  br label %common.ret

bb1:                                              ; preds = %0
  store i32 1, ptr %Q, align 4
  %B = load i32, ptr %Q, align 4
  call void @bar(i32 %B)
  br label %common.ret

bb2:                                              ; preds = %0
  store i32 1, ptr %Q, align 4
  %C = load i32, ptr %Q, align 4
  call void @bar(i32 %C)
  br label %common.ret
}

define void @test_switch_reach_terminator(i64 %i, ptr %p) {
  switch i64 %i, label %bb0 [
    i64 1, label %bb1
    i64 2, label %common.ret
  ]

common.ret:                                       ; preds = %0, %bb1, %bb0
  ret void

bb0:                                              ; preds = %0
  store i32 1, ptr %p, align 4
  br label %common.ret

bb1:                                              ; preds = %0
  store i32 2, ptr %p, align 4
  br label %common.ret
}

define i1 @common_instr_on_switch(i64 %a, i64 %b, i64 %c) unnamed_addr {
start:
  %0 = icmp eq i64 %b, %c
  ret i1 %0
}

define i1 @partial_common_instr_on_switch(i64 %a, i64 %b, i64 %c) unnamed_addr {
start:
  switch i64 %a, label %bb0 [
    i64 1, label %bb1
    i64 2, label %bb2
  ]

bb0:                                              ; preds = %start
  %0 = icmp eq i64 %b, %c
  br label %exit

bb1:                                              ; preds = %start
  %1 = icmp ne i64 %b, %c
  br label %exit

bb2:                                              ; preds = %start
  %2 = icmp eq i64 %b, %c
  br label %exit

exit:                                             ; preds = %bb2, %bb1, %bb0
  %result = phi i1 [ %0, %bb0 ], [ %1, %bb1 ], [ %2, %bb2 ]
  ret i1 %result
}

declare void @foo()

define i1 @test_icmp_simple(i1 %c, i32 %a, i32 %b) {
  br i1 %c, label %if, label %else

common.ret:                                       ; preds = %else, %if
  %common.ret.op = phi i1 [ %cmp1, %if ], [ %cmp2, %else ]
  ret i1 %common.ret.op

if:                                               ; preds = %0
  %cmp1 = icmp ult i32 %a, %b
  call void @foo()
  br label %common.ret

else:                                             ; preds = %0
  %cmp2 = icmp ugt i32 %b, %a
  call void @bar()
  br label %common.ret
}

define void @test_icmp_complex(i1 %c, i32 %a, i32 %b) {
  br i1 %c, label %if, label %else

if:                                               ; preds = %0
  %cmp1 = icmp ult i32 %a, %b
  br i1 %cmp1, label %if2, label %else2

else:                                             ; preds = %0
  %cmp2 = icmp ugt i32 %b, %a
  br i1 %cmp2, label %if2, label %else2

common.ret:                                       ; preds = %else2, %if2
  ret void

if2:                                              ; preds = %else, %if
  call void @foo()
  br label %common.ret

else2:                                            ; preds = %else, %if
  call void @bar()
  br label %common.ret
}

define i1 @test_icmp_wrong_operands(i1 %c, i32 %a, i32 %b) {
  br i1 %c, label %if, label %else

common.ret:                                       ; preds = %else, %if
  %common.ret.op = phi i1 [ %cmp1, %if ], [ %cmp2, %else ]
  ret i1 %common.ret.op

if:                                               ; preds = %0
  %cmp1 = icmp ult i32 %a, %b
  call void @foo()
  br label %common.ret

else:                                             ; preds = %0
  %cmp2 = icmp ugt i32 %a, %b
  call void @bar()
  br label %common.ret
}

define i1 @test_icmp_wrong_pred(i1 %c, i32 %a, i32 %b) {
  br i1 %c, label %if, label %else

common.ret:                                       ; preds = %else, %if
  %common.ret.op = phi i1 [ %cmp1, %if ], [ %cmp2, %else ]
  ret i1 %common.ret.op

if:                                               ; preds = %0
  %cmp1 = icmp ult i32 %a, %b
  call void @foo()
  br label %common.ret

else:                                             ; preds = %0
  %cmp2 = icmp uge i32 %b, %a
  call void @bar()
  br label %common.ret
}

define i32 @test_binop(i1 %c, i32 %a, i32 %b) {
  br i1 %c, label %if, label %else

common.ret:                                       ; preds = %else, %if
  %common.ret.op = phi i32 [ %op1, %if ], [ %op2, %else ]
  ret i32 %common.ret.op

if:                                               ; preds = %0
  %op1 = add i32 %a, %b
  call void @foo()
  br label %common.ret

else:                                             ; preds = %0
  %op2 = add i32 %b, %a
  call void @bar()
  br label %common.ret
}

define i32 @test_binop_flags(i1 %c, i32 %a, i32 %b) {
  br i1 %c, label %if, label %else

common.ret:                                       ; preds = %else, %if
  %common.ret.op = phi i32 [ %op1, %if ], [ %op2, %else ]
  ret i32 %common.ret.op

if:                                               ; preds = %0
  %op1 = add nuw nsw i32 %a, %b
  call void @foo()
  br label %common.ret

else:                                             ; preds = %0
  %op2 = add nsw i32 %b, %a
  call void @bar()
  br label %common.ret
}

define i32 @test_binop_not_commutative(i1 %c, i32 %a, i32 %b) {
  br i1 %c, label %if, label %else

common.ret:                                       ; preds = %else, %if
  %common.ret.op = phi i32 [ %op1, %if ], [ %op2, %else ]
  ret i32 %common.ret.op

if:                                               ; preds = %0
  %op1 = sub i32 %a, %b
  call void @foo()
  br label %common.ret

else:                                             ; preds = %0
  %op2 = sub i32 %b, %a
  call void @bar()
  br label %common.ret
}

define i32 @test_binop_wrong_ops(i1 %c, i32 %a, i32 %b, i32 %d) {
  br i1 %c, label %if, label %else

common.ret:                                       ; preds = %else, %if
  %common.ret.op = phi i32 [ %op1, %if ], [ %op2, %else ]
  ret i32 %common.ret.op

if:                                               ; preds = %0
  %op1 = add i32 %a, %b
  call void @foo()
  br label %common.ret

else:                                             ; preds = %0
  %op2 = add i32 %b, %d
  call void @bar()
  br label %common.ret
}

define i32 @test_intrin(i1 %c, i32 %a, i32 %b) {
  br i1 %c, label %if, label %else

common.ret:                                       ; preds = %else, %if
  %common.ret.op = phi i32 [ %op1, %if ], [ %op2, %else ]
  ret i32 %common.ret.op

if:                                               ; preds = %0
  %op1 = call i32 @llvm.umin.i32(i32 %a, i32 %b)
  call void @foo()
  br label %common.ret

else:                                             ; preds = %0
  %op2 = call i32 @llvm.umin.i32(i32 %b, i32 %a)
  call void @bar()
  br label %common.ret
}

define i32 @test_intrin_not_same(i1 %c, i32 %a, i32 %b) {
  br i1 %c, label %if, label %else

common.ret:                                       ; preds = %else, %if
  %common.ret.op = phi i32 [ %op1, %if ], [ %op2, %else ]
  ret i32 %common.ret.op

if:                                               ; preds = %0
  %op1 = call i32 @llvm.umin.i32(i32 %a, i32 %b)
  call void @foo()
  br label %common.ret

else:                                             ; preds = %0
  %op2 = call i32 @llvm.umax.i32(i32 %b, i32 %a)
  call void @bar()
  br label %common.ret
}

define float @test_intrin_3arg(i1 %c, float %a, float %b, float %d) {
  br i1 %c, label %if, label %else

common.ret:                                       ; preds = %else, %if
  %common.ret.op = phi float [ %op1, %if ], [ %op2, %else ]
  ret float %common.ret.op

if:                                               ; preds = %0
  %op1 = call float @llvm.fma.f32(float %a, float %b, float %d)
  call void @foo()
  br label %common.ret

else:                                             ; preds = %0
  %op2 = call float @llvm.fma.f32(float %b, float %a, float %d)
  call void @bar()
  br label %common.ret
}

define float @test_intrin_3arg_wrong_args_commuted(i1 %c, float %a, float %b, float %d) {
  br i1 %c, label %if, label %else

common.ret:                                       ; preds = %else, %if
  %common.ret.op = phi float [ %op1, %if ], [ %op2, %else ]
  ret float %common.ret.op

if:                                               ; preds = %0
  %op1 = call float @llvm.fma.f32(float %a, float %b, float %d)
  call void @foo()
  br label %common.ret

else:                                             ; preds = %0
  %op2 = call float @llvm.fma.f32(float %a, float %d, float %b)
  call void @bar()
  br label %common.ret
}

; Function Attrs: nocallback nofree nosync nounwind speculatable willreturn memory(none)
declare float @llvm.fma.f32(float, float, float) #0

; Function Attrs: nocallback nofree nosync nounwind speculatable willreturn memory(none)
declare i32 @llvm.umax.i32(i32, i32) #0

; Function Attrs: nocallback nofree nosync nounwind speculatable willreturn memory(none)
declare i32 @llvm.umin.i32(i32, i32) #0

attributes #0 = { nocallback nofree nosync nounwind speculatable willreturn memory(none) }
