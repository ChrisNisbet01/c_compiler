; ModuleID = 'c_compiler_module'
source_filename = "c_compiler_module"

define i32 @main() {
entry:
  %i = alloca i32, align 4
  store i32 42, ptr %i, align 4
  %load_tmp = load i32, ptr %i, align 4
  ret i32 %load_tmp
}
