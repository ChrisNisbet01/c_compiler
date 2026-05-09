NCC := ../build/src/ncc
SRC_DIR := ../src
GENERATED_DIR := ../build/src/generated
EASY_PC_INC := /home/chris/projects/easy_pc/include
EASY_PC_LIB := ../build/easy_pc-build/lib/libeasy_pc.so.0.2.0
LLVM_INC := /usr/include/llvm-c-20 /usr/include/llvm-20
LLVM_LDFLAGS := $(shell llvm-config-20 --ldflags)
LLVM_LIBS := $(shell llvm-config-20 --link-shared --libs)
CLANG := /usr/bin/clang-20
LDFLAGS := -Wl,-rpath,$(abspath $(dir $(EASY_PC_LIB)))
LINK := /usr/bin/clang-20

CFLAGS := -c -O3 -g

INCLUDES := -I$(GENERATED_DIR) -I$(SRC_DIR) -I$(EASY_PC_INC) $(addprefix -I,$(LLVM_INC))
DEFINES := -DWITH_INPUT_STREAM_SUPPORT

SRC_FILES := \
	main.c \
	ast_node_name.c \
	ast_print.c \
	compound_literal_processor.c \
	c_grammar_ast_actions.c \
	debug.c \
	declaration_handler.c \
	enum_evaluation.c \
	generator_lists.c \
	hash_table.c \
	ir_gen_error.c \
	ir_utils.c \
	labels.c \
	llvm_ir_generator.c \
	scope.c \
	scope_lists.c \
	symbol_table.c \
	typed_value.c \
	type_descriptors.c \
	type_qualifiers.c \
	type_specifier.c \
	type_utils.c \
	source_location.c \
	binary_expression_processor.c \
	unary_operations.c

GENERATED_SRC := c_grammar.c

ALL_LL := $(SRC_FILES:%.c=%.ll) $(GENERATED_SRC:%.c=%.ll)
ALL_O := $(ALL_LL:%.ll=%.o)
TARGET := ncc

.PHONY: all clean
.SECONDARY: $(ALL_LL)

all: $(TARGET)

$(TARGET): $(ALL_O)
	$(LINK) -o $@ $^ $(LDFLAGS) $(EASY_PC_LIB) $(LLVM_LDFLAGS) $(LLVM_LIBS) -lstdc++ -lm

%.ll: $(SRC_DIR)/%.c $(NCC)
	$(NCC) -S --debug=info --emit-llvm --emit-ast -o $@ $(INCLUDES) $(DEFINES) $<

c_grammar.ll: $(GENERATED_DIR)/c_grammar.c $(NCC)
	$(NCC) -S --debug=info --emit-llvm --emit-ast -o $@ $(INCLUDES) $(DEFINES) $<

%.o: %.ll
	$(CLANG) $(CFLAGS) -o $@ $<

clean:
	rm -f *.ll *.o $(TARGET)
