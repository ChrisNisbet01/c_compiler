#include "source_location.h"

#include "debug.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void
source_location_tracker_init(source_location_tracker_t * tracker)
{
    tracker->entries = NULL;
    tracker->count = 0;
    tracker->capacity = 0;
    tracker->include_stack = NULL;
    tracker->stack_top = 0;
    tracker->stack_capacity = 0;
}

void
source_location_tracker_free(source_location_tracker_t * tracker)
{
    if (tracker == NULL)
    {
        return;
    }
    for (size_t i = 0; i < tracker->count; i++)
    {
        free(tracker->entries[i].original_filename);
    }
    free(tracker->entries);

    for (size_t i = 0; i < tracker->stack_top; i++)
    {
        free(tracker->include_stack[i].filename);
    }
    free(tracker->include_stack);

    tracker->entries = NULL;
    tracker->count = 0;
    tracker->capacity = 0;
}

void
source_location_tracker_add_entry(
    source_location_tracker_t * tracker,
    epc_parser_input_view_t preprocessed_view,
    size_t original_line,
    char const * original_filename
)
{
    if (tracker == NULL)
    {
        return;
    }
    /* Resize if needed */
    if (tracker->count >= tracker->capacity)
    {
        size_t new_cap = tracker->capacity == 0 ? 8 : tracker->capacity * 2;
        source_location_entry_t * new_entries = realloc(tracker->entries, new_cap * sizeof(*new_entries));
        if (new_entries == NULL)
        {
            return;
        }
        tracker->entries = new_entries;
        tracker->capacity = new_cap;
    }

    /* Add entry (assume entries are added in increasing order of preprocessed_line) */
    source_location_entry_t * entry = &tracker->entries[tracker->count];
    entry->preprocessed_view = preprocessed_view;
    entry->original_line = original_line;
    entry->original_filename = strdup(original_filename);

    tracker->count++;
}

void
source_location_tracker_push_include(source_location_tracker_t * tracker, char const * filename, size_t line)
{
    if (tracker == NULL)
    {
        return;
    }

    if (tracker->stack_top >= tracker->stack_capacity)
    {
        size_t new_cap = tracker->stack_capacity == 0 ? 8 : tracker->stack_capacity * 2;
        include_stack_entry_t * new_stack = realloc(tracker->include_stack, new_cap * sizeof(*new_stack));
        if (new_stack == NULL)
        {
            return;
        }
        tracker->include_stack = new_stack;
        tracker->stack_capacity = new_cap;
    }

    include_stack_entry_t * entry = &tracker->include_stack[tracker->stack_top++];
    entry->filename = strdup(filename);
    entry->line = line;
}

void
source_location_tracker_pop_include(source_location_tracker_t * tracker)
{
    if (tracker == NULL || tracker->stack_top == 0)
    {
        return;
    }
    tracker->stack_top--;

    include_stack_entry_t * entry = &tracker->include_stack[tracker->stack_top];

    free(entry->filename);
}

source_location_entry_t const *
source_location_tracker_find(source_location_tracker_t const * tracker, size_t preprocessed_offset)
{
    if (tracker == NULL || tracker->count == 0)
    {
        return NULL;
    }

    int lo = 0;
    int hi = (int)tracker->count - 1;
    source_location_entry_t const * result = &tracker->entries[0];

    while (lo <= hi)
    {
        int mid = lo + (hi - lo) / 2;
        source_location_entry_t const * entry = &tracker->entries[mid];

        if (entry->preprocessed_view.offset <= preprocessed_offset)
        {
            result = entry;
            lo = mid + 1;
        }
        else
        {
            hi = mid - 1;
        }
    }

    return result;
}
