/**
 * @file gi_args.h
 * @brief Command-line argument parsing for Ghostimg.
 *
 * This module defines the CLI interface and populates a @ref GiArgs
 * structure from argc/argv. It performs no I/O and has no knowledge
 * of image formats or the UI layer.
 */

#ifndef GI_ARGS_H
#define GI_ARGS_H

#include "gi_image.h"

/**
 * @enum GiCommand
 * @brief Top-level command provided by the user.
 */
typedef enum {
  GI_CMD_UNKNOWN = 0, /**< No valid command recognized. */
  GI_CMD_INFO,        /**< Print metadata report without modifying files. */
  GI_CMD_CLEAN        /**< Remove metadata and optionally optimize. */
} GiCommand;

/**
 * @struct GiArgs
 * @brief Parsed representation of all CLI arguments.
 */
typedef struct {
  GiCommand command;  /**< Top-level command (info or clean). */
  GiOptMode opt_mode; /**< Processing strategy (lossless or lossy). */
  int quality;        /**< Re-encode quality 1-100 (lossy only, default 85). */
  int dry_run;        /**< If 1, preview changes without writing any file. */
  char **files;       /**< Input file or directory paths. */
  int file_count;     /**< Number of entries in @p files. */
} GiArgs;

/**
 * @brief Parses argc/argv into a @ref GiArgs structure.
 *
 * Recognized commands:
 *
 *   - @c info  <path>
 *
 *   - @c clean <path>
 *
 *   - @c clean <path> --dry
 *
 *   - @c clean <path> --lossy <quality>
 *
 * The @p files array inside @p args points directly into @p argv —
 * no heap allocation is performed for the strings themselves.
 * Call @ref gi_args_free when done to release the array.
 *
 * @param args  Output structure to populate.
 * @param argc  Argument count from main().
 * @param argv  Argument vector from main().
 * @return 0 on success, -1 on invalid or missing arguments.
 */
int gi_args_parse(GiArgs *args, int argc, char *argv[]);

/**
 * @brief Releases heap memory allocated by @ref gi_args_parse.
 * @param args Pointer to the structure to free.
 */
void gi_args_free(GiArgs *args);

#endif /* GI_ARGS_H */
