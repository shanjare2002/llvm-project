; ModuleID = 'basic.ll'
source_filename = "basic.ll"

@glob = global i8 1

define void @test() {
  ret void
}

; Function Attrs: nocallback nofree nosync nounwind willreturn memory(argmem: readwrite)
declare void @llvm.lifetime.start.p0(i64 immarg, ptr captures(none)) #0

; Function Attrs: nocallback nofree nosync nounwind willreturn memory(argmem: readwrite)
declare void @llvm.lifetime.end.p0(i64 immarg, ptr captures(none)) #0

define i32 @test_lifetime_alloca() {
  ret i32 0
}

define i32 @test_lifetime_arg(ptr %0) {
  ret i32 0
}

define i32 @test_lifetime_global() {
  ret i32 0
}

define i32 @test_lifetime_bitcast(ptr %arg) {
  %cast = bitcast ptr %arg to ptr
  call void @llvm.lifetime.start.p0(i64 -1, ptr %cast)
  call void @llvm.lifetime.end.p0(i64 -1, ptr %cast)
  ret i32 0
}

attributes #0 = { nocallback nofree nosync nounwind willreturn memory(argmem: readwrite) }
