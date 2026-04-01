#pragma once

#include <stdbool.h>
#include <stddef.h>

// --- Error and Warning Reporting ---

typedef struct ir_gen_error
{
    char * message;
} ir_gen_error_t;

typedef struct ir_gen_error_collection
{
    ir_gen_error_t * errors;
    size_t count;
    size_t capacity;
    size_t max_errors; // Threshold - stop after this many (e.g., 10)
    bool fatal;        // Set to true when max_errors exceeded
} ir_gen_error_collection_t;

/**
 * @brief Initializes an error collection.
 * @param collection The error collection to initialize.
 * @param max_errors The maximum number of errors to collect before marking as fatal.
 */
void ir_gen_error_collection_init(ir_gen_error_collection_t * collection, size_t max_errors);

/**
 * @brief Frees an error collection.
 * @param collection The error collection to free.
 */
void ir_gen_error_collection_free(ir_gen_error_collection_t * collection);

/**
 * @brief Adds an error to the collection and prints it.
 * @param collection The error collection.
 * @param fmt Format string for the error message.
 * @return true if the error limit has been reached (fatal), false otherwise.
 */
bool ir_gen_error(ir_gen_error_collection_t * collection, char const * fmt, ...);

/**
 * @brief Adds a warning to the collection and prints it.
 * @param collection The error collection.
 * @param fmt Format string for the warning message.
 */
void ir_gen_warning(ir_gen_error_collection_t * collection, char const * fmt, ...);

/**
 * @brief Checks if any errors have been collected.
 * @param collection The error collection.
 * @return true if errors were collected, false otherwise.
 */
bool ir_gen_has_errors(ir_gen_error_collection_t * collection);

/**
 * @brief Checks if the error collection is in fatal state.
 * @param collection The error collection.
 * @return true if fatal (error limit reached), false otherwise.
 */
bool ir_gen_is_fatal(ir_gen_error_collection_t * collection);
