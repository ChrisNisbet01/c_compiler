; ModuleID = 'c_compiler_module'
source_filename = "c_compiler_module"

@str_tmp = private unnamed_addr constant [18 x i8] c"matrix[0][0]: %d\0A\00", align 1
@str_tmp.1 = private unnamed_addr constant [18 x i8] c"matrix[0][1]: %d\0A\00", align 1
@str_tmp.2 = private unnamed_addr constant [18 x i8] c"matrix[1][0]: %d\0A\00", align 1
@str_tmp.3 = private unnamed_addr constant [18 x i8] c"matrix[1][2]: %d\0A\00", align 1
@str_tmp.4 = private unnamed_addr constant [37 x i8] c"matrix[0][2] after modification: %d\0A\00", align 1
@str_tmp.5 = private unnamed_addr constant [32 x i8] c"Size of matrix (in bytes): %zu\0A\00", align 1
@str_tmp.6 = private unnamed_addr constant [35 x i8] c"Size of matrix[0] (in bytes): %zu\0A\00", align 1

define i32 @main() {
entry:
  %matrix = alloca [2 x [3 x i32]], align 4
  store i32 1, ptr %matrix, align 4
  %load_tmp = load [2 x [3 x i32]], ptr %matrix, align 4
  %arrayidx = getelementptr inbounds [2 x [3 x i32]], ptr %matrix, i32 0, i32 0
  %arrayelem = load [3 x i32], ptr %arrayidx, align 4
  %arrayidx1 = getelementptr inbounds [2 x [3 x i32]], ptr %matrix, i32 0, i32 0
  %arrayelem2 = load [3 x i32], ptr %arrayidx1, align 4
  %call_tmp = call i32 (ptr, [3 x i32], ...) @printf(ptr @str_tmp, [3 x i32] %arrayelem2)
  %load_tmp3 = load [2 x [3 x i32]], ptr %matrix, align 4
  %arrayidx4 = getelementptr inbounds [2 x [3 x i32]], ptr %matrix, i32 0, i32 0
  %arrayelem5 = load [3 x i32], ptr %arrayidx4, align 4
  %arrayidx6 = getelementptr inbounds [2 x [3 x i32]], ptr %matrix, i32 0, i32 1
  %arrayelem7 = load [3 x i32], ptr %arrayidx6, align 4
  %call_tmp8 = call i32 (ptr, [3 x i32], ...) @printf(ptr @str_tmp.1, [3 x i32] %arrayelem7)
  %load_tmp9 = load [2 x [3 x i32]], ptr %matrix, align 4
  %arrayidx10 = getelementptr inbounds [2 x [3 x i32]], ptr %matrix, i32 0, i32 1
  %arrayelem11 = load [3 x i32], ptr %arrayidx10, align 4
  %arrayidx12 = getelementptr inbounds [2 x [3 x i32]], ptr %matrix, i32 0, i32 0
  %arrayelem13 = load [3 x i32], ptr %arrayidx12, align 4
  %call_tmp14 = call i32 (ptr, [3 x i32], ...) @printf(ptr @str_tmp.2, [3 x i32] %arrayelem13)
  %load_tmp15 = load [2 x [3 x i32]], ptr %matrix, align 4
  %arrayidx16 = getelementptr inbounds [2 x [3 x i32]], ptr %matrix, i32 0, i32 1
  %arrayelem17 = load [3 x i32], ptr %arrayidx16, align 4
  %arrayidx18 = getelementptr inbounds [2 x [3 x i32]], ptr %matrix, i32 0, i32 2
  %arrayelem19 = load [3 x i32], ptr %arrayidx18, align 4
  %call_tmp20 = call i32 (ptr, [3 x i32], ...) @printf(ptr @str_tmp.3, [3 x i32] %arrayelem19)
  %arrayidx21 = getelementptr inbounds [3 x i32], ptr %matrix, i32 0, i32 0
  %arrayidx22 = getelementptr inbounds i32, ptr %arrayidx21, i32 0, i32 2
  store i32 30, ptr %arrayidx22, align 4
  %load_tmp23 = load [2 x [3 x i32]], ptr %matrix, align 4
  %arrayidx24 = getelementptr inbounds [2 x [3 x i32]], ptr %matrix, i32 0, i32 0
  %arrayelem25 = load [3 x i32], ptr %arrayidx24, align 4
  %arrayidx26 = getelementptr inbounds [2 x [3 x i32]], ptr %matrix, i32 0, i32 2
  %arrayelem27 = load [3 x i32], ptr %arrayidx26, align 4
  %call_tmp28 = call i32 (ptr, [3 x i32], ...) @printf(ptr @str_tmp.4, [3 x i32] %arrayelem27)
  %load_tmp29 = load [2 x [3 x i32]], ptr %matrix, align 4
  %call_tmp30 = call i32 (ptr, [3 x i32], ...) @printf(ptr @str_tmp.5, [2 x [3 x i32]] %load_tmp29)
  %load_tmp31 = load [2 x [3 x i32]], ptr %matrix, align 4
  %arrayidx32 = getelementptr inbounds [2 x [3 x i32]], ptr %matrix, i32 0, i32 0
  %arrayelem33 = load [3 x i32], ptr %arrayidx32, align 4
  %call_tmp34 = call i32 (ptr, [3 x i32], ...) @printf(ptr @str_tmp.6, [3 x i32] %arrayelem33)
  ret i32 0
}

declare i32 @printf(ptr, [3 x i32], ...)
