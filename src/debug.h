/**
 * @file debug.h
 * @brief Debug output utilities for NCC compiler.
 *
 * Provides debug/warning/error message printing with configurable verbosity levels.
 * These are for internal developer debugging, not compiler errors/warnings for user code.
 */

#pragma once

#include <stdbool.h>

/**
 * @brief Debug level enumeration.
 */
typedef enum
{
    DEBUG_LEVEL_OFF,     // No debug output
    DEBUG_LEVEL_INFO,    // Informational messages
    DEBUG_LEVEL_WARNING, // Warnings and above
    DEBUG_LEVEL_ERROR    // Errors only
} debug_level_t;

/**
 * @brief Sets the debug output level.
 * @param level The debug level to set.
 */
void debug_set_level(debug_level_t level);

/**
 * @brief Gets the current debug level.
 * @return The current debug level.
 */
debug_level_t debug_get_level(void);

/**
 * @brief Checks if debug output at the given level is enabled.
 * @param level The debug level to check.
 * @return true if output is enabled for this level.
 */
bool debug_is_enabled(debug_level_t level);

/**
 * @brief Prints a debug message at INFO level.
 * @param fmt Format string.
 * @param ... Variable arguments.
 */
void debug_info(char const * fmt, ...);

/**
 * @brief Prints a debug message at WARNING level.
 * @param fmt Format string.
 * @param ... Variable arguments.
 */
void debug_warning(char const * fmt, ...);

/**
 * @brief Prints a debug message at ERROR level.
 * @param fmt Format string.
 * @param ... Variable arguments.
 */
void debug_error_int(char const * func, int line, char const * fmt, ...);
#define debug_error(fmt, ...) debug_error_int(__func__, __LINE__, (fmt), ##__VA_ARGS__)