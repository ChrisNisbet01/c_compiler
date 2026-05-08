#pragma once

#include "llvm_ir_generator.h"

#include <llvm-c/Core.h>
#include <stdint.h>

typedef struct TypeDescriptor_st TypeDescriptor;

char * generate_anon_name(ir_generator_ctx_t * ctx, char const * prefix);

uint64_t get_type_alignment(ir_generator_ctx_t * ctx, LLVMTypeRef type);

uint64_t get_type_size(ir_generator_ctx_t * ctx, TypeDescriptor const * type);

TypedValue cast_typed_value_to_desc(ir_generator_ctx_t * ctx, TypedValue src, TypeDescriptor const * target_desc);

void aligned_store(
    ir_generator_ctx_t * ctx, LLVMBuilderRef builder, LLVMValueRef value, LLVMTypeRef value_type, LLVMValueRef ptr
);

// Helper wrapper for LLVMBuildLoad2 with proper alignment
LLVMValueRef
aligned_load(ir_generator_ctx_t * ctx, LLVMBuilderRef builder, LLVMTypeRef ty, LLVMValueRef ptr, char const * name);
