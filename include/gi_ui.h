/**
 * @file gi_ui.h
 * @brief Terminal presentation layer for Ghostimg.
 *
 * All user-facing output lives here. No other module may print
 * directly to stdout/stderr except through these functions.
 *
 * Color output is automatically disabled when stdout is not a TTY
 * (piped output, CI environments, redirected logs).
 */

#ifndef GI_UI_H
#define GI_UI_H

#include <stddef.h>

/**
 * @brief Initializes the UI layer.
 *
 * Detects TTY support and sets color/no-color mode accordingly.
 * Must be called once before any other gi_ui_* function.
 */
void gi_ui_init(void);

/**
 * @brief Prints an informational message to stdout.
 * @param msg Message text.
 */
void gi_ui_info(const char *msg);

/**
 * @brief Prints a success message to stdout.
 * @param msg Message text.
 */
void gi_ui_success(const char *msg);

/**
 * @brief Prints a warning message to stdout.
 * @param msg Message text.
 */
void gi_ui_warn(const char *msg);

/**
 * @brief Prints an error message to stderr.
 * @param msg Message text.
 */
void gi_ui_error(const char *msg);

/**
 * @brief Prints a processing start line for a single file.
 *
 * Called before each file is processed in a batch run.
 *
 * @param path     File path being processed.
 * @param index    Current file index (1-based).
 * @param total    Total number of files in the batch.
 */
void gi_ui_progress(const char *path, int index, int total);

/**
 * @brief Prints the final batch summary line.
 *
 * @param ok           Number of files processed successfully.
 * @param total        Total number of files attempted.
 * @param bytes_before Total bytes before processing.
 * @param bytes_after  Total bytes after processing.
 */
void gi_ui_summary(int ok, int total, size_t bytes_before, size_t bytes_after);

/**
 * @brief Prints the full help text to stdout.
 * @param version Application version string.
 */
void gi_ui_help(const char *version);

/**
 * @brief Prints the version string to stdout.
 * @param version Application version string.
 */
void gi_ui_version(const char *version);

#endif /* GI_UI_H */
