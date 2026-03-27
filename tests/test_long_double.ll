; ModuleID = 'c_compiler_module'
source_filename = "c_compiler_module"

@str_tmp = private unnamed_addr constant [8 x i8] c"d: %Lf\0A\00", align 1

define i32 @main() {
entry:
  %d = alloca double, align 8
  store x86_fp80 0xK3FF0DF2310A640710800, ptr %d, align 16
  %load_tmp = load double, ptr %d, align 8
  %call_tmp = call i32 (...) @printf(ptr @str_tmp, double %load_tmp)
  ret i32 0
}

declare i32 @printf(...)
