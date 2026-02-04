; ModuleID = 'output1.ll'
source_filename = "basictest.ll"

define i32 @Test(i32 %A, i32 %B) {
BB1:
  br label %BB4

BB2:                                              ; No predecessors!
  br label %BB3

BB3:                                              ; preds = %BB4, %BB2
  %ret = phi i32 [ %X, %BB4 ], [ %B, %BB2 ]
  ret i32 %ret

BB4:                                              ; preds = %BB1
  %X = phi i32 [ %A, %BB1 ]
  br label %BB3
}
