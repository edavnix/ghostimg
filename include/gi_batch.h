/**
 * @file gi_batch.h
 * @brief Batch processing orchestration for Ghostimg.
 *
 * This module is the single entry point between main() and the
 * format-specific parsers. It resolves input paths (files and
 * directories), dispatches each image to the correct parser,
 * passes results to the UI layer, and reports a final summary.
 *
 * It has no knowledge of image internals — it delegates entirely
 * to jpeg.c, png.c and webp.c via gi_platform for type detection.
 * All user-facing output is routed through gi_ui and gi_ui_report.
 */

#ifndef GI_BATCH_H
#define GI_BATCH_H

#include "gi_args.h"

/**
 * @brief Processes all inputs described in @p args.
 *
 * For each entry in @p args->files:
 * - If it is a directory, collects all images recursively via
 *   @ref gi_platform_collect_images and processes each one.
 * - If it is a regular file, processes it directly.
 *
 * Output is written in-place (src == dst) since @ref GiArgs
 * no longer carries an output directory.
 *
 * @param args Parsed CLI arguments from @ref gi_args_parse.
 * @return 0 if all files processed successfully, 1 if any failed.
 */
int gi_batch_run(const GiArgs *args);

#endif /* GI_BATCH_H */
