#include "type_descriptors.h"

#include "debug.h"

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
    builtin_types builtins;
} TypeDescriptors;

static void
dump_type_descriptor(TypeDescriptor const * desc, debug_level_t level)
{
    if (level < debug_get_level())
    {
        return;
    }
    fprintf(
        stderr,
        "TypeDescriptor: kind=%d llvm_type_kind=%d\n",
        desc->kind,
        desc->llvm_type != NULL ? (int)LLVMGetTypeKind(desc->llvm_type) : -1
    );
}

// Internal helper to allocate and link a new descriptor
static TypeDescriptor *
register_descriptor(TypeDescriptors * registry, TypeDescriptor const * template)
{
    debug_info("%s: Registering new descriptor kind %d", __func__, template->kind);
    dump_type_descriptor(template, DEBUG_LEVEL_INFO);

    TypeDescriptor_private * node = malloc(sizeof(*node));
    node->public = *template;
    debug_info("%s: new node %p", __func__, (void *)&node->public);
    type_specifier_dump(node->public.specifiers, DEBUG_LEVEL_INFO);

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
            && curr->public.array_size == size)
        {
            return &curr->public;
        }
        curr = curr->next;
    }

    TypeDescriptor template
        = {.kind = NCC_TYPE_KIND_ARRAY,
           .llvm_type = LLVMArrayType(element_type->llvm_type, (unsigned)size),
           .pointee = element_type,
           .array_size = size};
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
    LLVMTypeRef llvm_type = NULL;
    if (specs.is_void)
        llvm_type = registry->builtins.void_type;
    else if (specs.is_float)
        llvm_type = registry->builtins.f32_type;
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
    else if (specs.long_count > 1)
        llvm_type = registry->builtins.i64_type;
    else if (specs.is_int)
        llvm_type = registry->builtins.i32_type;
    else if (specs.is_short)
        llvm_type = registry->builtins.i16_type;
    else if (specs.is_char)
        llvm_type = registry->builtins.i8_type;
    else if (specs.is_bool)
        llvm_type = registry->builtins.i1_type;
    else if (specs.is_unsigned)
        llvm_type = registry->builtins.i32_type; // Unsigned int is still i32 in LLVM, the signedness is in the
                                                 // operations, not the type itself
    else // TODO: Determine if long should be i64 or i32 depending on architecture
        llvm_type = registry->builtins.i32_type; // Default int / long

    TypeDescriptor template = {
        .kind = NCC_TYPE_KIND_BUILTIN, .llvm_type = llvm_type, .specifiers = specs, .qualifiers = quals, .pointee = NULL
    };

    return register_descriptor(registry, &template);
}

TypeDescriptor const *
register_struct_type(TypeDescriptors * registry, LLVMTypeRef llvm_struct, TypeQualifier const quals, bool is_union)
{
    // Check if this LLVM type is already wrapped
    TypeDescriptor_private * curr = registry->head;
    while (curr != NULL)
    {
        if (curr->public.llvm_type == llvm_struct)
            return &curr->public;
        curr = curr->next;
    }

    TypeDescriptor template = {
        .kind = is_union ? NCC_TYPE_KIND_UNION : NCC_TYPE_KIND_STRUCT,
        .llvm_type = llvm_struct,
        .pointee = NULL, // Structs aren't pointers
        .qualifiers = quals,
    };

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
        free(cur);
        cur = next;
    }
    free(registry);
}

TypeDescriptors *
type_descriptors_create_registry(LLVMContextRef context)
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
    builtins_init(registry);

    return registry;
}

TypeDescriptor const *
get_or_create_function_type(
    TypeDescriptors * registry,
    TypeDescriptor const * ret_type,
    TypeDescriptor const ** params,
    char const ** param_names,
    size_t param_count,
    bool is_variadic
)
{
    TypeDescriptor_private * curr = registry->head;
    while (curr != NULL)
    {
        if (curr->public.kind == NCC_TYPE_KIND_FUNCTION && curr->public.function_metadata.return_type == ret_type
            && curr->public.function_metadata.param_count == param_count)
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
    TypeDescriptor template
        = {.kind = NCC_TYPE_KIND_FUNCTION,
           .pointee = ret_type,
           .function_metadata.return_type = ret_type,
           .function_metadata.param_count = param_count,
           .function_metadata.params = malloc(sizeof(*params) * param_count),
           .function_metadata.names = calloc(param_count, sizeof(*param_names)),
           .function_metadata.is_variadic = is_variadic};

    memcpy(template.function_metadata.params, params, sizeof(*params) * param_count);
    for (size_t i = 0; i < param_count; i++)
    {
        if (param_names[i] != NULL)
        {
            template.function_metadata.names[i] = strdup(param_names[i]);
        }
    }

    // Construct the LLVM function type
    LLVMTypeRef * llvm_params = malloc(sizeof(*llvm_params) * param_count);
    for (unsigned i = 0; i < param_count; i++)
    {
        llvm_params[i] = params[i]->llvm_type;
    }
    debug_info(
        "%s: creating function type for return type %d with %zu params",
        __func__,
        LLVMGetTypeKind(ret_type->llvm_type),
        param_count
    );
    template.llvm_type = LLVMFunctionType(ret_type->llvm_type, llvm_params, param_count, is_variadic);
    debug_info(
        "%s: created function type %d for return type %d with %zu params",
        __func__,
        LLVMGetTypeKind(template.llvm_type),
        LLVMGetTypeKind(ret_type->llvm_type),
        param_count
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
create_fallback_descriptor_impl(TypeDescriptors * registry, LLVMTypeRef llvm_type, char const * func, int line)
{
    debug_warning(
        "%s:%d: No descriptor found for LLVM type %d, creating fallback", func, line, LLVMGetTypeKind(llvm_type)
    );
    TypeDescriptor template = {0};
    template.llvm_type = llvm_type;

    LLVMTypeKind kind = LLVMGetTypeKind(llvm_type);

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wswitch-enum"

    switch (kind)
    {
    case LLVMIntegerTypeKind:
        template.kind = NCC_TYPE_KIND_BUILTIN; // Or a generic INT kind
        break;
    case LLVMPointerTypeKind:
        template.kind = NCC_TYPE_KIND_POINTER;
        // Note: We can't easily know the 'pointee' here
        // without a deep search or recursive fallback.
        break;
    case LLVMArrayTypeKind:
        template.kind = NCC_TYPE_KIND_ARRAY;
        template.array_size = LLVMGetArrayLength(llvm_type);
        break;
    case LLVMStructTypeKind:
        template.kind = NCC_TYPE_KIND_STRUCT;
        break;
    case LLVMVoidTypeKind:
        template.kind = NCC_TYPE_KIND_BUILTIN;
        break;
    case LLVMFunctionTypeKind:
        template.kind = NCC_TYPE_KIND_FUNCTION;
        break;
    default:
        break;
    }

#pragma GCC diagnostic pop

    // Register it so we don't create duplicates for the same orphan type
    return register_descriptor(registry, &template);
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
