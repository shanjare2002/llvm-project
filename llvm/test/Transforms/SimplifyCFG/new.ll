; ModuleID = 'switch-branch-fold-indirectbr-102351.ll'
source_filename = "switch-branch-fold-indirectbr-102351.ll"

define i32 @foo.1(i32 %arg, ptr %arg1) {
bb:
  %alloca = alloca [2 x ptr], align 16
  store ptr blockaddress(@foo.1, %bb2), ptr %alloca, align 16
  %getelementptr = getelementptr inbounds [2 x ptr], ptr %alloca, i64 0, i64 1
  store ptr blockaddress(@foo.1, %bb16), ptr %getelementptr, align 8
  br label %bb2

bb2:                                              ; preds = %bb18, %bb
  %phi = phi i32 [ 0, %bb ], [ 2, %bb18 ]
  %phi3 = phi i32 [ 0, %bb ], [ %arg, %bb18 ]
  switch i32 %phi, label %bb2.unreachabledefault [
    i32 0, label %bb18
    i32 2, label %bb11
  ]

bb11:                                             ; preds = %bb2
  %call = call i32 @wombat(i32 noundef %phi3)
  %add = add nsw i32 %phi3, 1
  br label %bb18

bb2.unreachabledefault:                           ; preds = %bb2
  unreachable

bb16:                                             ; preds = %bb18
  %call17 = call i32 @wombat(i32 noundef %arg)
  ret i32 0

bb18:                                             ; preds = %bb2, %bb11
  %load = load ptr, ptr %arg1, align 8
  indirectbr ptr %load, [label %bb2, label %bb16]
}

declare i32 @wombat(i32)
