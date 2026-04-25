; ModuleID = '../new_tests/test_char.c'
source_filename = "../new_tests/test_char.c"

@__FILE__ = private constant [25 x i8] c"../new_tests/test_char.c\00"
@__FUNC__ = private constant [5 x i8] c"main\00"

define void @main() {
entry:
  %i = alloca i32, align 4
  ret void
}
