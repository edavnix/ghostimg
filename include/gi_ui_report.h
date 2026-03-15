/**
 * @file gi_ui_report.h
 * @brief Formatted report output for Ghostimg.
 *
 * This module renders structured data (GiImageInfo, GiCleanResult)
 * into human-readable terminal output. It is the only module allowed
 * to format and print image-specific information.
 *
 * All functions receive pure data structs — no file I/O is performed here.
 */

#ifndef GI_UI_REPORT_H
#define GI_UI_REPORT_H

#include "gi_image.h"

/**
 * @brief Prints a detailed metadata report for an image.
 *
 * Used by the @c info command. Displays format, dimensions, file size,
 * and all detected metadata fields (EXIF, GPS, XMP, ICC, comments).
 *
 * @param info Populated image info struct from a parser.
 */
void gi_ui_report_info(const GiImageInfo *info);

/**
 * @brief Prints the result of a clean or dry-run operation.
 *
 * Used by @c clean, @c clean --dry and @c clean --lossy.
 * When @p result->dry_run is 1, output uses "would save" language.
 * When @p result->success is 0, prints the error description.
 *
 * @param result Populated clean result struct from a parser.
 */
void gi_ui_report_clean(const GiCleanResult *result);

#endif /* GI_UI_REPORT_H */
