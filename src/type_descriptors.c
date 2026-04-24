#include "type_descriptors.h"

#include "builtin_types.h"

#include <stdbool.h>
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
    LLVMContextRef * context;
    builtin_types builtins;
} TypeDescriptors;

// Internal helper to allocate and link a new descriptor
static TypeDescriptor *
register_descriptor(TypeDescriptors * registry, TypeDescriptor const * template)
{
    TypeDescriptor_private * node = malloc(sizeof(*node));
    node->public = *template;

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

// 1. Get or Create a Pointer Type
TypeDescriptor const *
get_or_create_pointer_type(TypeDescriptors * registry, TypeDescriptor const * pointee, TypeQualifier qualifiers)
{
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
get_or_create_builtin_type(TypeDescriptors * registry, TypeSpecifier const specs, TypeQualifier const quals)
{
    TypeDescriptor_private * curr = registry->head;
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
    else if (specs.is_char)
        llvm_type = registry->builtins.i8_type;
    else if (specs.is_bool)
        llvm_type = registry->builtins.i1_type;
    else                                         // TODO: Determine if long should be i64 or i32 depending on ?
        llvm_type = registry->builtins.i32_type; // Default int / long

    TypeDescriptor template = {
        .kind = NCC_TYPE_KIND_BUILTIN, .llvm_type = llvm_type, .specifiers = specs, .qualifiers = quals, .pointee = NULL
    };
    return register_descriptor(registry, &template);
}

static void
builtins_init(TypeDescriptors * registry)
{
    builtin_types * types = &registry->builtins;
    LLVMContextRef * context = registry->context;

    types->i1_type = LLVMInt1TypeInContext(context);
    types->i8_type = LLVMInt8TypeInContext(context);
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
    spec.is_bool = true;
    get_or_create_builtin_type(registry, spec, (TypeQualifier){0});
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
type_descriptors_create_registry(LLVMContextRef * context)
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
