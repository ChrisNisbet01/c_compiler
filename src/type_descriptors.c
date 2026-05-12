#include "type_descriptors.h"

#include "debug.h"
#include "type_utils.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct TypeDescriptor_private TypeDescriptor_private;
struct TypeDescriptor_private
{
    TypeDescriptor public;
    struct TypeDescriptor_private * next;
};

typedef struct builtin_types
{
    LLVMTypeRef i1_type;
    LLVMTypeRef i8_type;
    LLVMTypeRef i16_type;
    LLVMTypeRef i32_type;
    LLVMTypeRef i64_type;
    LLVMTypeRef ptr_type;
    LLVMTypeRef f32_type;
    LLVMTypeRef f64_type;
    LLVMTypeRef long_double_type;
    LLVMTypeRef void_type;
} builtin_types;

typedef struct TypeDescriptors
{
    TypeDescriptor_private * head;
    LLVMContextRef context;
    LLVMTargetDataRef data_layout;
    LLVMBuilderRef builder;
    builtin_types builtins;
} TypeDescriptors;

static unsigned
get_fp_width(LLVMTypeRef type)
{
    LLVMTypeKind kind = LLVMGetTypeKind(type);

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wswitch-enum"

    switch (kind)
    {
    case LLVMHalfTypeKind:
        return 16;
    case LLVMFloatTypeKind:
        return 32;
    case LLVMDoubleTypeKind:
        return 64;
    case LLVMX86_FP80TypeKind:
        return 80;
    case LLVMFP128TypeKind:
        return 128;
    default:
        return 0; // Not a floating-point type
    }

#pragma GCC diagnostic pop
}

// Internal helper to allocate and link a new descriptor
static TypeDescriptor *
register_descriptor(TypeDescriptors * registry, TypeDescriptor const * template)
{
    debug_info("%s: Registering new descriptor kind %d", __func__, template->kind);
    dump_type_descriptor(__func__, template, DEBUG_LEVEL_INFO);

    TypeDescriptor_private * node = malloc(sizeof(*node));
    node->public = *template;
    debug_info("%s: new node %p", __func__, (void *)&node->public);
    dump_type_descriptor(__func__, &node->public, DEBUG_LEVEL_INFO);

    type_specifier_dump(node->public.specifiers, DEBUG_LEVEL_INFO);

    LLVMTypeKind llvm_kind = LLVMGetTypeKind(node->public.llvm_type);
    if (llvm_kind == LLVMIntegerTypeKind)
    {
        node->public.integer_metadata.width = LLVMGetIntTypeWidth(node->public.llvm_type);
    }
    else if (llvm_kind == LLVMFloatTypeKind)
    {
        node->public.float_metadata.width = get_fp_width(node->public.llvm_type);
    }

    node->next = registry->head;
    registry->head = node;

    return &node->public;
}
static bool
qualifiers_match(TypeQualifier const * a, TypeQualifier const * b)
{
    TypeQualifier default_qualifiers = {0};
    if (a == NULL)
    {
        a = &default_qualifiers;
    }
    if (b == NULL)
    {
        b = &default_qualifiers;
    }
    return memcmp(a, b, sizeof(*a)) == 0;
}

static bool
specifiers_match(TypeSpecifier const * a, TypeSpecifier const * b)
{
    TypeSpecifier default_specifiers = {0};
    if (a == NULL)
    {
        a = &default_specifiers;
    }
    if (b == NULL)
    {
        b = &default_specifiers;
    }
    return memcmp(a, b, sizeof(*a)) == 0;
}

TypeDescriptor const *
get_or_create_array_type(TypeDescriptors * registry, TypeDescriptor const * element_type, size_t size)
{
    TypeDescriptor_private * curr = registry->head;
    while (curr)
    {
        if (curr->public.kind == NCC_TYPE_KIND_ARRAY && curr->public.pointee == element_type
            && curr->public.array_metadata.size == size)
        {
            return &curr->public;
        }
        curr = curr->next;
    }

    TypeDescriptor template
        = {.kind = NCC_TYPE_KIND_ARRAY,
           .llvm_type = LLVMArrayType(element_type->llvm_type, (unsigned)size),
           .pointee = element_type,
           .array_metadata.size = size};
    return register_descriptor(registry, &template);
}

TypeDescriptor const *
get_or_create_pointer_type(TypeDescriptors * registry, TypeDescriptor const * pointee, TypeQualifier qualifiers)
{
    debug_info("%s", __func__);
    // Search for existing pointer to this EXACT pointee with these qualifiers
    TypeDescriptor_private * curr = registry->head;
    while (curr)
    {
        if (curr->public.kind == NCC_TYPE_KIND_POINTER && curr->public.pointee == pointee
            && qualifiers_match(&curr->public.qualifiers, &qualifiers))
        {
            return &curr->public;
        }
        curr = curr->next;
    }

    // Create new pointer descriptor (LLVM 18+ uses 'ptr')
    TypeDescriptor template
        = {.kind = NCC_TYPE_KIND_POINTER,
           .llvm_type = registry->builtins.ptr_type,
           .pointee = pointee,
           .qualifiers = qualifiers};

    return register_descriptor(registry, &template);
}

static TypeDescriptor const *
type_descriptor_base(TypeDescriptor const * desc)
{
    if (desc == NULL)
    {
        return NULL;
    }
    while (desc->base != NULL)
    {
        desc = desc->base;
    }
    return desc;
}

TypeDescriptor const *
get_or_create_qualified_type(
    TypeDescriptors * registry, TypeDescriptor const * unqualified_type, TypeQualifier qualifiers
)
{
    TypeDescriptor const * base_type = type_descriptor_base(unqualified_type);

    if (base_type == NULL)
    {
        return NULL;
    }
    debug_info(
        "%s: base: %p, is_const: %d, is_volatile: %d",
        __func__,
        (void *)base_type,
        qualifiers.is_const,
        qualifiers.is_volatile
    );

    if (!qualifiers.is_const && !qualifiers.is_volatile)
    {
        return base_type;
    }

    TypeDescriptor_private * curr = registry->head;
    while (curr)
    {
        TypeDescriptor const * curr_base = type_descriptor_base(&curr->public);

        if (curr_base == base_type && qualifiers_match(&curr->public.qualifiers, &qualifiers))
        {
            debug_info(
                "%s: existing (%p) curr base num_members: %zu",
                __func__,
                (void *)&curr->public,
                curr_base->struct_metadata.members.num_members
            );
            return &curr->public;
        }
        curr = curr->next;
    }
    debug_info("%s: base num_members: %zu", __func__, base_type->struct_metadata.members.num_members);
    TypeDescriptor template = *base_type;
    template.base = base_type;
    template.qualifiers.is_const |= qualifiers.is_const;
    template.qualifiers.is_volatile |= qualifiers.is_volatile;

    TypeDescriptor const * new_desc = register_descriptor(registry, &template);

    debug_info("%s: new num_members: %zu", __func__, base_type->struct_metadata.members.num_members);

    return new_desc;
}

static LLVMTypeRef
type_ref_from_specs(TypeDescriptors * registry, TypeSpecifier const specs)
{
    LLVMTypeRef llvm_type;

    if (specs.is_void)
    {
        llvm_type = registry->builtins.void_type;
    }
    else if (specs.is_float)
    {
        llvm_type = registry->builtins.f32_type;
    }
    else if (specs.is_double)
    {
        if (specs.long_count > 1)
        {
            llvm_type = registry->builtins.long_double_type;
        }
        else
        {
            llvm_type = registry->builtins.f64_type;
        }
    }
    else if (specs.long_count > 0)
    {
        llvm_type = registry->builtins.i64_type;
    }
    else if (specs.is_int)
    {
        llvm_type = registry->builtins.i32_type;
    }
    else if (specs.is_short)
    {
        llvm_type = registry->builtins.i16_type;
    }
    else if (specs.is_char)
    {
        llvm_type = registry->builtins.i8_type;
    }
    else if (specs.is_bool)
    {
        llvm_type = registry->builtins.i1_type;
    }
    else if (specs.is_unsigned)
    {
        llvm_type = registry->builtins.i32_type; // Unsigned int is still i32 in LLVM, the signedness is in the
                                                 // operations, not the type itself
    }
    else // TODO: Determine if long should be i64 or i32 depending on architecture
    {
        llvm_type = registry->builtins.i32_type; // Default int / long
    }

    return llvm_type;
}

TypeDescriptor const *
get_or_create_builtin_type(TypeDescriptors * registry, TypeSpecifier const specs_in, TypeQualifier const quals)
{
    TypeSpecifier specs = specs_in;

    debug_info("%s", __func__);
    TypeDescriptor_private * curr = registry->head;

    if (memcmp(&specs, &(TypeSpecifier){0}, sizeof(specs)) == 0)
    {
        debug_info("%s: No specifiers provided. Defaulting to int", __func__);
        specs.is_int = true;
    }

    while (curr)
    {
        if (curr->public.kind == NCC_TYPE_KIND_BUILTIN && specifiers_match(&curr->public.specifiers, &specs)
            && qualifiers_match(&curr->public.qualifiers, &quals))
        {
            return &curr->public;
        }
        curr = curr->next;
    }

    // New builtin: Determine LLVM type based on specifiers
    LLVMTypeRef llvm_type = type_ref_from_specs(registry, specs);
    TypeDescriptor template
        = {.kind = NCC_TYPE_KIND_BUILTIN, .llvm_type = llvm_type, .specifiers = specs, .qualifiers = quals};

    return register_descriptor(registry, &template);
}

/**
 * @brief Calculates size and sets member offsets for Structs and Unions.
 * This should be called once the TypeDescriptor's member list is fully populated.
 */

static void
calculate_composite_size(LLVMTargetDataRef data_layout, TypeDescriptor * const desc)
{
    debug_info("%s", __func__);
    if (desc == NULL || (desc->kind != NCC_TYPE_KIND_STRUCT && desc->kind != NCC_TYPE_KIND_UNION))
    {
        debug_error("%s: bad type descriptor");
        return;
    }

    uint64_t current_offset = 0;
    uint32_t max_align = 1;
    size_t member_count = desc->struct_metadata.members.num_members;

    if (member_count == 0)
    {
        return;
    }

    // First pass: find max size and max alignment per storage_index
    // (needed for flattened unions where multiple members share a storage_index)
    unsigned num_storage_indices = desc->struct_metadata.members.members[member_count - 1].bitfield.storage_index + 1;
    uint64_t * max_sizes = calloc(num_storage_indices, sizeof(*max_sizes));
    uint32_t * max_aligns = calloc(num_storage_indices, sizeof(*max_aligns));

    for (size_t i = 0; i < member_count; ++i)
    {
        struct_field_t * current_member = &desc->struct_metadata.members.members[i];
        TypeDescriptor const * mem_desc = current_member->type_desc;
        unsigned sidx = current_member->bitfield.storage_index;

        uint64_t mem_size = get_type_size_desc(data_layout, mem_desc);
        uint32_t mem_align = get_type_alignment_desc(mem_desc);

        if (mem_size > max_sizes[sidx])
        {
            max_sizes[sidx] = mem_size;
        }
        if (mem_align > max_aligns[sidx])
        {
            max_aligns[sidx] = mem_align;
        }
        if (mem_align > max_align)
        {
            max_align = mem_align;
        }
    }

    // Second pass: compute offsets and total size
    unsigned current_sidx = desc->struct_metadata.members.members[0].bitfield.storage_index;

    for (size_t i = 0; i < member_count; ++i)
    {
        struct_field_t * current_member = &desc->struct_metadata.members.members[i];
        TypeDescriptor const * mem_desc = current_member->type_desc;
        unsigned sidx = current_member->bitfield.storage_index;

        if (desc->kind == NCC_TYPE_KIND_STRUCT)
        {
            if (sidx != current_sidx)
            {
                // Finalize previous storage unit: advance cursor by its max size
                current_offset += max_sizes[current_sidx];
                current_sidx = sidx;

                // Add padding for the new storage unit using its max alignment
                uint64_t padding = (max_aligns[sidx] - (current_offset % max_aligns[sidx])) % max_aligns[sidx];
                current_offset += padding;
            }

            // Special case: for the very first member, add padding if needed
            if (i == 0)
            {
                uint64_t padding = (max_aligns[sidx] - (current_offset % max_aligns[sidx])) % max_aligns[sidx];
                current_offset += padding;
            }

            // Set the offset for this member (all union members share same offset)
            desc->struct_metadata.members.members[i].bitfield.offset = current_offset;
            debug_info(
                "member: %zu offset: %llu size: %llu align: %u storage: %u",
                i,
                current_offset,
                get_type_size_desc(data_layout, mem_desc),
                get_type_alignment_desc(mem_desc),
                current_member->bitfield.storage_index
            );
        }
        else
        {
            // UNION: All members start at offset 0
            desc->struct_metadata.members.members[i].bitfield.offset = 0;
        }
    }

    // Finalize: for structs, advance past the last storage unit; for unions, take overall max
    if (desc->kind == NCC_TYPE_KIND_STRUCT)
    {
        current_offset += max_sizes[current_sidx];
    }
    else
    {
        current_offset = 0;
        for (unsigned i = 0; i < num_storage_indices; i++)
        {
            if (max_sizes[i] > current_offset)
            {
                current_offset = max_sizes[i];
            }
        }
    }

    free(max_sizes);
    free(max_aligns);

    // 4. Final Tail Padding
    // The total size must be a multiple of the largest alignment found
    uint64_t tail_padding = (max_align - (current_offset % max_align)) % max_align;
    desc->struct_metadata.total_size = current_offset + tail_padding;
    desc->struct_metadata.alignment = max_align;
    debug_info(
        "%s: total size: %llu, alignment: %u",
        __func__,
        desc->struct_metadata.total_size,
        desc->struct_metadata.alignment
    );
}

TypeDescriptor const *
register_struct_type(
    TypeDescriptors * registry,
    LLVMTypeRef llvm_struct,
    TypeQualifier const quals,
    bool is_union,
    bool is_complete,
    struct_or_union_members_st const * members
)
{
    // Check if this LLVM type is already wrapped
    TypeDescriptor_private * curr = registry->head;
    while (curr != NULL)
    {
        if (curr->public.llvm_type == llvm_struct)
            return &curr->public;
        curr = curr->next;
    }

    /* When registering an opaque struct there may be no members passed in. */
    struct_or_union_members_st members_template = {0};
    if (members != NULL && members->num_members > 0)
    {
        members_template.num_members = members->num_members;
        members_template.members = malloc(sizeof(*members->members) * members->num_members);
        memcpy(members_template.members, members->members, members->num_members * sizeof(*members->members));
        /* We need to have our own copy of the name. */
        for (size_t i = 0; i < members_template.num_members; i++)
        {
            if (members_template.members[i].name != NULL)
            {
                members_template.members[i].name = strdup(members_template.members[i].name);
            }
        }
    }

    TypeDescriptor template
        = {.kind = is_union ? NCC_TYPE_KIND_UNION : NCC_TYPE_KIND_STRUCT,
           .llvm_type = llvm_struct,
           .qualifiers = quals,
           .struct_metadata.is_complete = is_complete,
           .struct_metadata.members = members_template};
    calculate_composite_size(registry->data_layout, &template);

    return register_descriptor(registry, &template);
}

static void
builtins_init(TypeDescriptors * registry)
{
    builtin_types * types = &registry->builtins;
    LLVMContextRef context = registry->context;

    types->i1_type = LLVMInt1TypeInContext(context);
    types->i8_type = LLVMInt8TypeInContext(context);
    types->i16_type = LLVMInt16TypeInContext(context);
    types->i32_type = LLVMInt32TypeInContext(context);
    types->i64_type = LLVMInt64TypeInContext(context);
    types->ptr_type = LLVMPointerTypeInContext(context, 0);
    types->f32_type = LLVMFloatTypeInContext(context);
    types->f64_type = LLVMDoubleTypeInContext(context);
    types->long_double_type = LLVMX86FP80TypeInContext(context);
    types->void_type = LLVMVoidTypeInContext(context);

    TypeSpecifier spec;
    memset(&spec, 0, sizeof(spec));
    /* Now register them all with the type system. */
    spec.is_void = true;
    get_or_create_builtin_type(registry, spec, (TypeQualifier){0});
    spec.is_void = false;
    spec.is_bool = true;
    get_or_create_builtin_type(registry, spec, (TypeQualifier){0});
    spec.is_bool = false;
    spec.is_char = true;
    get_or_create_builtin_type(registry, spec, (TypeQualifier){0});
    spec.is_char = false;
    spec.is_short = true;
    get_or_create_builtin_type(registry, spec, (TypeQualifier){0});
    spec.is_short = false;

    for (int i = 0; i < 2; i++)
    {
        spec.is_int = i;
        if (i > 0)
        {
            spec.long_count = 0;
            get_or_create_builtin_type(registry, spec, (TypeQualifier){0});
        }
        spec.long_count = 1;
        get_or_create_builtin_type(registry, spec, (TypeQualifier){0});
        spec.long_count = 2;
        get_or_create_builtin_type(registry, spec, (TypeQualifier){0});
    }
    spec.long_count = 0;
    spec.is_int = false;

    spec.is_double = true;
    get_or_create_builtin_type(registry, spec, (TypeQualifier){0});
    spec.long_count = 1;
    get_or_create_builtin_type(registry, spec, (TypeQualifier){0});
    spec.long_count = 0;
    spec.is_double = false;

    spec.is_float = true;
    get_or_create_builtin_type(registry, spec, (TypeQualifier){0});
    spec.is_float = false;
}

void
type_descriptors_destroy_registry(TypeDescriptors * registry)
{
    if (registry == NULL)
    {
        return;
    }
    TypeDescriptor_private * cur = registry->head;
    while (cur != NULL)
    {
        TypeDescriptor_private * next = cur->next;
        // Free function metadata coerced arrays
        if (cur->public.kind == NCC_TYPE_KIND_FUNCTION)
        {
            free(cur->public.function_metadata.params);
            free(cur->public.function_metadata.coerced_param_counts);
            free(cur->public.function_metadata.coerced_params);
        }
        free(cur);
        cur = next;
    }
    free(registry);
}

TypeDescriptors *
type_descriptors_create_registry(LLVMContextRef context, LLVMTargetDataRef data_layout, LLVMBuilderRef builder)
{
    if (context == NULL)
    {
        return NULL;
    }

    TypeDescriptors * registry = calloc(1, sizeof(*registry));

    if (registry == NULL)
    {
        return NULL;
    }
    registry->context = context;
    registry->data_layout = data_layout;
    registry->builder = builder;

    builtins_init(registry);

    return registry;
}

void
stitch_param_part(TypeDescriptors * registry, LLVMValueRef struct_alloca, LLVMValueRef raw_param, int part_idx)
{
    // We use GEP to find the exact byte offset in the struct memory
    // part_idx 0 = offset 0
    // part_idx 1 = offset 8 (the second Eightbyte)

    // We can't always use StructGEP because the 'coerced' type might not match
    // the C struct member exactly. We use a Byte-aligned GEP (i8*) instead.
    LLVMValueRef offset = LLVMConstInt(registry->builtins.i32_type, part_idx * 8, false);

    // Get pointer to the specific eightbyte chunk
    LLVMValueRef byte_ptr = LLVMBuildInBoundsGEP2(
        registry->builder, registry->builtins.i8_type, struct_alloca, &offset, 1, "abi_stitch_ptr"
    );

    // Cast the byte_ptr to the type of the register value we are storing
    LLVMValueRef typed_ptr
        = LLVMBuildBitCast(registry->builder, byte_ptr, LLVMPointerType(LLVMTypeOf(raw_param), 0), "abi_cast_ptr");

    // Store the register value into the struct memory
    LLVMBuildStore(registry->builder, raw_param, typed_ptr);
}

CoercedType
get_coerced_llvm_types(TypeDescriptors * registry, TypeDescriptor const * td)
{
    CoercedType result = {.count = 0};
    uint64_t size = get_type_size_desc(registry->data_layout, td);

    // Rule 1: Not a struct/union? It's a simple scalar (int, ptr, float).
    if (td->kind != NCC_TYPE_KIND_STRUCT && td->kind != NCC_TYPE_KIND_UNION)
    {
        result.types[0] = td->llvm_type;
        result.count = 1;
        return result;
    }

    // Rule 2: Large Structs (> 16 bytes)
    // These are passed by a single hidden pointer.
    if (size > 16)
    {
        result.types[0] = LLVMPointerType(LLVMInt8TypeInContext(registry->context), 0);
        result.count = 1;
        return result;
    }

    // Rule 3: Small Structs (1-8 bytes)
    if (size <= 8)
    {
        result.types[0] = LLVMIntTypeInContext(registry->context, (unsigned)(size * 8));
        result.count = 1;
        return result;
    }

    // Rule 4: Medium Structs (9-16 bytes) - Split into two Eightbytes
    result.types[0] = LLVMInt64TypeInContext(registry->context); // First 8 bytes

    // For the second part, use an integer that covers the remainder
    uint64_t remaining = size - 8;
    result.types[1] = LLVMIntTypeInContext(registry->context, (unsigned)(remaining * 8));
    result.count = 2;

    return result;
}

TypeDescriptor const *
get_or_create_function_type(
    TypeDescriptors * registry,
    TypeDescriptor const * ret_type,
    TypeDescriptor const ** params,
    size_t param_count,
    bool is_variadic
)
{
    TypeDescriptor_private * curr = registry->head;
    while (curr != NULL)
    {
        if (curr->public.kind == NCC_TYPE_KIND_FUNCTION && curr->public.function_metadata.return_type == ret_type
            && curr->public.function_metadata.param_count == param_count
            && curr->public.function_metadata.is_variadic == is_variadic)
        {
            // Check if all parameters match exactly
            bool match = true;
            for (size_t i = 0; i < param_count; i++)
            {
                if (curr->public.function_metadata.params[i] != params[i])
                {
                    match = false;
                    break;
                }
            }
            if (match)
            {
                return &curr->public;
            }
        }
        curr = curr->next;
    }

    // Not found: Create a new function descriptor
    // First pass: compute coerced types for each parameter
    int * coerced_counts = NULL;
    CoercedType * coerced_params = NULL;
    size_t total_coerced = 0;

    if (param_count > 0)
    {
        coerced_counts = calloc(param_count, sizeof(*coerced_counts));
        coerced_params = calloc(param_count, sizeof(*coerced_params));

        for (unsigned i = 0; i < param_count; i++)
        {
            CoercedType ct = get_coerced_llvm_types(registry, params[i]);
            coerced_counts[i] = ct.count;
            coerced_params[i] = ct;
            total_coerced += (size_t)ct.count;
        }
    }

    TypeDescriptor template = {
        .kind = NCC_TYPE_KIND_FUNCTION,
        .pointee = ret_type,
        .function_metadata.return_type = ret_type,
        .function_metadata.param_count = (unsigned)total_coerced, // Total LLVM params (coerced)
        .function_metadata.is_variadic = is_variadic,
        .function_metadata.is_void_return = LLVMGetTypeKind(ret_type->llvm_type) == LLVMVoidTypeKind,
        .function_metadata.coerced_param_counts = coerced_counts,
        .function_metadata.coerced_params = coerced_params,
    };

    if (param_count > 0)
    {
        template.function_metadata.params = calloc(param_count, sizeof(*params));
        memcpy(template.function_metadata.params, params, sizeof(*params) * param_count);
    }

    // Construct the LLVM function type using coerced param types
    LLVMTypeRef * llvm_params = NULL;
    if (total_coerced > 0)
    {
        llvm_params = malloc(sizeof(*llvm_params) * total_coerced);
        size_t idx = 0;
        for (unsigned i = 0; i < param_count; i++)
        {
            CoercedType ct = coerced_params[i];
            for (int j = 0; j < ct.count; j++)
            {
                llvm_params[idx++] = ct.types[j];
            }
        }
    }
    debug_info(
        "%s: creating function type for return type %d with %zu LLVM params (was %zu C params), is variadic: %d",
        __func__,
        LLVMGetTypeKind(ret_type->llvm_type),
        total_coerced,
        param_count,
        is_variadic
    );
    template.llvm_type = LLVMFunctionType(ret_type->llvm_type, llvm_params, (unsigned)total_coerced, is_variadic);
    debug_info(
        "%s: created function type %d for return type %d with %zu LLVM params",
        __func__,
        LLVMGetTypeKind(template.llvm_type),
        LLVMGetTypeKind(ret_type->llvm_type),
        total_coerced
    );

    free(llvm_params);
    return register_descriptor(registry, &template);
}

/* This function shouldn't be required once the conversion to the new type system is complete. */
TypeDescriptor const *
get_type_descriptor_from_specifiers(TypeDescriptors * registry, TypeSpecifier const specs, TypeQualifier const quals)
{
    debug_info("%s", __func__);
    type_specifier_dump(specs, DEBUG_LEVEL_INFO);

    TypeDescriptor_private * curr = registry->head;
    while (curr)
    {
        if (curr->public.kind == NCC_TYPE_KIND_BUILTIN && specifiers_match(&curr->public.specifiers, &specs)
            && qualifiers_match(&curr->public.qualifiers, &quals))
        {
            debug_info("%s: found: %p", __func__, (void *)&curr->public);
            return &curr->public;
        }
        curr = curr->next;
    }
    return NULL;
}

TypeDescriptor const *
find_descriptor_by_llvm_type(TypeDescriptors * registry, LLVMTypeRef type)
{
    // Iterate through your type registry head
    TypeDescriptor_private * curr = registry->head;
    while (curr)
    {
        if (curr->public.llvm_type == type)
        {
            return &curr->public;
        }
        curr = curr->next;
    }

    return NULL;
}

TypeDescriptor const *
type_descriptor_get_uint64_type(TypeDescriptors * registry, bool const_qualified)
{
    return get_or_create_builtin_type(
        registry, (TypeSpecifier){.is_unsigned = true, .long_count = 2}, (TypeQualifier){.is_const = const_qualified}
    );
}

TypeDescriptor const *
type_descriptor_get_uint32_type(TypeDescriptors * registry, bool const_qualified)
{
    return get_or_create_builtin_type(
        registry, (TypeSpecifier){.is_unsigned = true}, (TypeQualifier){.is_const = const_qualified}
    );
}

TypeDescriptor const *
type_descriptor_get_uint8_type(TypeDescriptors * registry, bool const_qualified)
{
    return get_or_create_builtin_type(
        registry, (TypeSpecifier){.is_unsigned = true, .is_char = true}, (TypeQualifier){.is_const = const_qualified}
    );
}

TypeDescriptor const *
type_descriptor_get_int64_type(TypeDescriptors * registry, bool const_qualified)
{
    return get_or_create_builtin_type(
        registry, (TypeSpecifier){.long_count = 2}, (TypeQualifier){.is_const = const_qualified}
    );
}

TypeDescriptor const *
type_descriptor_get_int32_type(TypeDescriptors * registry, bool const_qualified)
{
    return get_or_create_builtin_type(
        registry, (TypeSpecifier){.is_int = true}, (TypeQualifier){.is_const = const_qualified}
    );
}

TypeDescriptor const *
type_descriptor_get_int8_type(TypeDescriptors * registry, bool const_qualified)
{
    return get_or_create_builtin_type(
        registry, (TypeSpecifier){.is_char = true}, (TypeQualifier){.is_const = const_qualified}
    );
}

TypeDescriptor const *
type_descriptor_get_bool_type(TypeDescriptors * registry, bool const_qualified)
{
    return get_or_create_builtin_type(
        registry, (TypeSpecifier){.is_bool = true}, (TypeQualifier){.is_const = const_qualified}
    );
}

TypeDescriptor const *
type_descriptor_get_void_type(TypeDescriptors * registry)
{
    return get_or_create_builtin_type(registry, (TypeSpecifier){.is_void = true}, (TypeQualifier){0});
}

static int
type_descriptor_find_struct_field_index_from_base_desc(TypeDescriptor const * base, char const * name)
{
    debug_info("%s: base desc: %p num_members: %zu", __func__, base, base->struct_metadata.members.num_members);
    // Access the members list in your metadata structure
    for (size_t i = 0; i < base->struct_metadata.members.num_members; ++i)
    {
        char const * member_name = base->struct_metadata.members.members[i].name;
        debug_info("check member: %s", member_name);
        if (member_name != NULL && strcmp(member_name, name) == 0)
        {
            debug_info("%s: got member %s at index %zu", __func__, name, i);
            dump_type_descriptor(name, base, DEBUG_LEVEL_INFO);
            return (int)i;
        }
    }

    // Field not found in this struct
    return -1;
}

int
type_descriptor_find_struct_field_index_from_desc(TypeDescriptor const * desc, char const * name)
{
    if (desc == NULL || name == NULL || (desc->kind != NCC_TYPE_KIND_STRUCT && desc->kind != NCC_TYPE_KIND_UNION))
    {
        debug_error("%s: Invalid struct descriptor", __func__);
        return -1;
    }

    TypeDescriptor const * base = type_descriptor_base(desc);

    return type_descriptor_find_struct_field_index_from_base_desc(base, name);
}

struct_field_t const *
type_descriptor_get_struct_field_type(TypeDescriptor const * desc, int index)
{
    if (desc == NULL || (desc->kind != NCC_TYPE_KIND_STRUCT && desc->kind != NCC_TYPE_KIND_UNION))
    {
        debug_warning("%s: Invalid struct descriptor", __func__);
        return NULL;
    }

    TypeDescriptor const * base = type_descriptor_base(desc);

    if (index < 0 || index >= (int)base->struct_metadata.members.num_members)
    {
        debug_warning(
            "%s: Index out of bounds: %d, masx: %zu", __func__, index, base->struct_metadata.members.num_members
        );
        return NULL;
    }
    struct_field_t * member = &base->struct_metadata.members.members[index];

    return member;
}

bool
is_integer_kind(TypeDescriptor const * desc)
{
    LLVMTypeKind kind = LLVMGetTypeKind(desc->llvm_type);
    return kind == LLVMIntegerTypeKind || kind == LLVMHalfTypeKind;
}

bool
is_floating_kind(TypeDescriptor const * desc)
{
    return get_fp_width(desc->llvm_type) > 0;
}

bool
is_void_return(TypeDescriptor const * desc)
{
    return desc->kind == NCC_TYPE_KIND_FUNCTION && desc->function_metadata.is_void_return;
}

uint64_t
get_type_size_desc(LLVMTargetDataRef data_layout, TypeDescriptor const * desc_in)
{
    TypeDescriptor const * desc = type_descriptor_base(desc_in);

    debug_info("%s, desc: %p", __func__, desc);
    if (desc == NULL)
    {
        return 0;
    }

    LLVMTypeKind llvm_kind = LLVMGetTypeKind(desc->llvm_type);

    if (llvm_kind == LLVMVoidTypeKind || desc->specifiers.is_void)
    {
        debug_info("is void");
        return 0;
    }

    if (is_integer_kind(desc))
    {
        unsigned width = LLVMGetIntTypeWidth(desc->llvm_type);
        if (width > 32)
        {
            return 8;
        }
        if (width > 16)
        {
            return 4;
        }
        if (width > 8)
        {
            return 2;
        }
        return 1;
    }

    if (is_floating_kind(desc))
    {
        if (llvm_kind == LLVMFloatTypeKind)
            return 4;
        return 8;
    }

    switch (desc->kind)
    {
    case NCC_TYPE_KIND_FUNCTION:
    case NCC_TYPE_KIND_POINTER:
        return 8; // Assuming 64-bit target

    case NCC_TYPE_KIND_ARRAY:
        return desc->array_metadata.size * get_type_size_desc(data_layout, desc->pointee);

    case NCC_TYPE_KIND_BUILTIN: /* Should have been caught by integer and float handling above. */
        debug_warning("Unknown type kind for size, defaulting to ABI");
        return LLVMABISizeOfType(data_layout, desc->llvm_type);

    case NCC_TYPE_KIND_UNION:
    case NCC_TYPE_KIND_STRUCT:
        // This needs to account for padding!
        // Better to use LLVM's offset calculation if the struct is already lowered,
        // or your own offset tracking in the descriptor.
        return desc->struct_metadata.total_size;

    default:
        // Fallback to LLVM's target data if available
        return LLVMABISizeOfType(data_layout, desc->llvm_type);
    }
}

uint32_t
get_type_alignment_desc(TypeDescriptor const * desc_in)
{
    TypeDescriptor const * desc = type_descriptor_base(desc_in);

    if (desc == NULL)
    {
        return 1;
    }

    LLVMTypeKind llvm_kind = LLVMGetTypeKind(desc->llvm_type);

    if (llvm_kind == LLVMVoidTypeKind || desc->specifiers.is_void)
    {
        return 1;
    }

    if (is_integer_kind(desc))
    {
        unsigned width = LLVMGetIntTypeWidth(desc->llvm_type);
        if (width > 32)
        {
            return 8;
        }
        if (width > 16)
        {
            return 4;
        }
        if (width > 8)
        {
            return 2;
        }
        return 1;
    }

    if (is_floating_kind(desc))
    {
        if (llvm_kind == LLVMFloatTypeKind)
            return 4;
        return 8;
    }

    switch (desc->kind)
    {
    case NCC_TYPE_KIND_POINTER:
    case NCC_TYPE_KIND_FUNCTION:
        // On 64-bit systems, pointers and 64-bit ints are 8-byte aligned
        return 8;

    case NCC_TYPE_KIND_ARRAY:
        // Array alignment is the alignment of its element type
        return get_type_alignment_desc(desc->pointee);

    case NCC_TYPE_KIND_STRUCT:
    case NCC_TYPE_KIND_UNION:
    {
        // Struct/Union alignment is the maximum alignment of any member
        uint32_t max_align = 1;
        for (size_t i = 0; i < desc->struct_metadata.members.num_members; ++i)
        {
            uint32_t member_align = get_type_alignment_desc(desc->struct_metadata.members.members[i].type_desc);
            if (member_align > max_align)
            {
                max_align = member_align;
            }
        }
        return max_align;
    }

    case NCC_TYPE_KIND_BUILTIN: /* Should have been caught by integer and float handling above. */
        debug_warning("Unknown builtin type kind %d for alignment, defaulting to 1", desc->kind);
        return 1;

    default:
        debug_warning("Unknown type kind %d for alignment, defaulting to 1", desc->kind);
        return 1;
    }
}

void
dump_type_descriptor(char const * name, TypeDescriptor const * desc, debug_level_t level)
{
    if (desc == NULL)
    {
        return;
    }

    if (!debug_is_enabled(level))
    {
        return;
    }

    fprintf(
        stderr,
        "TypeDescriptor: '%s', (%p), base: (%p), kind=%d, llvm_type_kind=%d, pointee (%p) kind: %d\n",
        name,
        (void *)desc,
        (void *)type_descriptor_base(desc),
        desc->kind,
        desc->llvm_type != NULL ? (int)LLVMGetTypeKind(desc->llvm_type) : -1,
        (void *)desc->pointee,
        desc->pointee != NULL && desc->pointee->llvm_type != NULL ? (int)LLVMGetTypeKind(desc->pointee->llvm_type) : -1
    );

    type_specifier_dump(desc->specifiers, level);
    type_qualifiers_dump(desc->qualifiers, level);
}

void
type_descriptor_complete_struct(
    TypeDescriptors * registry, TypeDescriptor const * type_desc_in, struct_or_union_members_st const * members
)
{
    debug_info("%s: type desc %p", __func__, type_desc_in);
    if (type_desc_in == NULL
        || (type_desc_in->kind != NCC_TYPE_KIND_STRUCT && type_desc_in->kind != NCC_TYPE_KIND_UNION))
    {
        debug_error("%s: Invalid struct descriptor", __func__);
        return;
    }
    TypeDescriptor * type_desc = (TypeDescriptor *)type_descriptor_base(type_desc_in);

    type_desc->struct_metadata.is_complete = true;

    if (members->num_members > 0)
    {
        type_desc->struct_metadata.members.num_members = members->num_members;
        type_desc->struct_metadata.members.members
            = malloc(sizeof(*type_desc->struct_metadata.members.members) * members->num_members);
        memcpy(
            type_desc->struct_metadata.members.members,
            members->members,
            members->num_members * sizeof(*members->members)
        );
        for (size_t i = 0; i < members->num_members; i++)
        {
            if (type_desc->struct_metadata.members.members[i].name != NULL)
            {
                type_desc->struct_metadata.members.members[i].name
                    = strdup(type_desc->struct_metadata.members.members[i].name);
            }
        }
    }
    calculate_composite_size(registry->data_layout, type_desc);
}

TypeDescriptor const *
type_descriptor_get_enum_type(TypeDescriptors * registry)
{
    return type_descriptor_get_int32_type(registry, false);
}

bool
type_descriptor_is_complete(TypeDescriptor const * type_desc_in)
{
    TypeDescriptor * type_desc = (TypeDescriptor *)type_descriptor_base(type_desc_in);
    if (type_desc == NULL)
    {
        return false;
    }

    bool is_complete = false;

    if (type_desc->kind == NCC_TYPE_KIND_STRUCT || type_desc->kind == NCC_TYPE_KIND_UNION)
    {
        is_complete = type_desc->struct_metadata.is_complete && type_desc->struct_metadata.members.num_members > 0;
    }

    return is_complete;
}