; ModuleID = 'atomic.ll'
source_filename = "atomic.ll"
target datalayout = "e-p:64:64:64-i1:8:8-i8:8:8-i16:16:16-i32:32:32-i64:64:64-f32:32:32-f64:64:64-v64:64:64-v128:128:128-a0:0:64-s0:64:64-f80:128:128-n8:16:32:64"
target triple = "x86_64-apple-macosx10.7.0"

@x = common global i32 0, align 4
@y = common global i32 0, align 4
@z = common global i64 0, align 4
@a = common global i64 0, align 4

declare void @randomop(ptr)

define void @test1() {
  store atomic i32 0, ptr @y unordered, align 4
  store i32 1, ptr @x, align 4
  ret void
}

define void @test4() {
  store i32 1, ptr @x, align 4
  ret void
}

define void @test5() {
  store atomic i32 1, ptr @x unordered, align 4
  ret void
}

define void @test6() {
  ret void
}

define void @test7() {
  %a = alloca i32, align 4
  store atomic i32 0, ptr %a seq_cst, align 4
  ret void
}

define i32 @test8() {
  %a = alloca i32, align 4
  call void @randomop(ptr %a)
  store i32 0, ptr %a, align 4
  %x = load atomic i32, ptr @x seq_cst, align 4
  ret i32 %x
}

define void @test10() {
  store atomic i32 42, ptr @y monotonic, align 4
  store i32 1, ptr @x, align 4
  ret void
}

define i32 @test11() {
  store atomic i32 0, ptr @x monotonic, align 4
  %x = load atomic i32, ptr @y monotonic, align 4
  store atomic i32 1, ptr @x monotonic, align 4
  ret i32 %x
}

define void @test12() {
  store atomic i32 0, ptr @x monotonic, align 4
  store atomic i32 42, ptr @y monotonic, align 4
  store atomic i32 1, ptr @x monotonic, align 4
  ret void
}

define i32 @test15() {
  store i32 0, ptr @x, align 4
  store atomic i32 0, ptr @y release, align 4
  %x = load atomic i32, ptr @y acquire, align 4
  store i32 1, ptr @x, align 4
  ret i32 %x
}

define i64 @test_atomicrmw_0() {
  store i64 1, ptr @z, align 8
  %res = atomicrmw add ptr @z, i64 -1 monotonic, align 8
  ret i64 %res
}

define i64 @test_atomicrmw_1() {
  store i64 1, ptr @z, align 8
  %res = atomicrmw add ptr @z, i64 -1 acq_rel, align 8
  ret i64 %res
}

define i64 @test_atomicrmw_2() {
  %res = atomicrmw add ptr @a, i64 -1 monotonic, align 8
  store i64 2, ptr @z, align 8
  ret i64 %res
}

define i64 @test_atomicrmw_3() {
  store i64 1, ptr @z, align 8
  %res = atomicrmw add ptr @a, i64 -1 release, align 8
  store i64 2, ptr @z, align 8
  ret i64 %res
}

define i64 @test_atomicrmw_4(ptr %ptr) {
  store i64 1, ptr @z, align 8
  %res = atomicrmw add ptr %ptr, i64 -1 monotonic, align 8
  store i64 2, ptr @z, align 8
  ret i64 %res
}

define i64 @test_atomicrmw_5() {
  store i64 1, ptr @z, align 8
  %res = atomicrmw add ptr @z, i64 -1 monotonic, align 8
  store i64 2, ptr @z, align 8
  ret i64 %res
}

define { i32, i1 } @test_cmpxchg_1() {
  store i32 1, ptr @x, align 4
  %ret = cmpxchg volatile ptr @x, i32 10, i32 20 seq_cst monotonic, align 4
  store i32 2, ptr @x, align 4
  ret { i32, i1 } %ret
}

define { i32, i1 } @test_cmpxchg_2() {
  %ret = cmpxchg volatile ptr @y, i32 10, i32 20 monotonic monotonic, align 4
  store i32 2, ptr @x, align 4
  ret { i32, i1 } %ret
}

define { i32, i1 } @test_cmpxchg_3() {
  store i32 1, ptr @x, align 4
  %ret = cmpxchg volatile ptr @y, i32 10, i32 20 seq_cst seq_cst, align 4
  store i32 2, ptr @x, align 4
  ret { i32, i1 } %ret
}

define { i32, i1 } @test_cmpxchg_4(ptr %ptr) {
  store i32 1, ptr @x, align 4
  %ret = cmpxchg volatile ptr %ptr, i32 10, i32 20 monotonic monotonic, align 4
  store i32 2, ptr @x, align 4
  ret { i32, i1 } %ret
}

define { i32, i1 } @test_cmpxchg_5(ptr %ptr) {
  store i32 1, ptr @x, align 4
  %ret = cmpxchg volatile ptr @x, i32 10, i32 20 monotonic monotonic, align 4
  store i32 2, ptr @x, align 4
  ret { i32, i1 } %ret
}

define void @test_load_atomic(ptr %Q) {
  ret void
}

define void @test_store_atomic(ptr %Q) {
  ret void
}

define void @test_store_atomic_release(ptr %Q) {
  %a = load i32, ptr %Q, align 4
  store atomic i32 %a, ptr %Q release, align 4
  ret void
}
