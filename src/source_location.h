#pragma once
#include <stddef.h>

/* A single entry in the source location map */
typedef struct source_location_entry_t
{
    size_t preprocessed_offset; /* Offset in preprocessed file (key for lookup) */
    size_t original_line;       /* Line number in original source file */
    char * original_filename;   /* Original source filename */
} source_location_entry_t;

/* Include stack entry for "In file included from..." messages */
typedef struct include_stack_entry_t
{
    char * filename;
    size_t line;
} include_stack_entry_t;

/* The tracker that maps preprocessed locations to original locations */
typedef struct source_location_tracker_t
{
    source_location_entry_t * entries; /* Sorted array by preprocessed_line */
    size_t count;
    size_t capacity;

    /* Include stack for "In file included from..." */
    include_stack_entry_t * include_stack;
    size_t stack_top;
    size_t stack_capacity;
} source_location_tracker_t;

void source_location_tracker_init(source_location_tracker_t * tracker);
void source_location_tracker_free(source_location_tracker_t * tracker);

/* Add a mapping entry. Called during the separate AST pass. */
void source_location_tracker_add_entry(
    source_location_tracker_t * tracker,
    size_t preprocessed_offset,
    size_t original_line,
    char const * original_filename
);

/* Push/pop for include stack (using flags 1 and 2) */
void source_location_tracker_push_include(source_location_tracker_t * tracker, char const * filename, size_t line);
void source_location_tracker_pop_include(source_location_tracker_t * tracker);

/* Binary search: find entry with largest preprocessed_line <= search_line */
source_location_entry_t const *
source_location_tracker_find(source_location_tracker_t const * tracker, size_t preprocessed_offset);
