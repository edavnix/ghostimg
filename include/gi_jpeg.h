/**
 * @file gi_jpeg.h
 * @brief JPEG metadata removal and optimization interface.
 *
 * This module handles segment-based parsing of JPEG files to
 * strip metadata and optionally re-encode image data via libjpeg-turbo.
 *
 * All functions return pure data structs — no output is printed here.
 * The caller (batch.c) passes results to the UI layer for display.
 *
 * Lossless path: walks raw JPEG markers, drops non-image segments.
 *   Pixel data is copied verbatim — no quality loss.
 *
 * Lossy path: full decode via libjpeg-turbo, strip all metadata,
 *   re-encode at target quality. Uses 4:2:2 chroma subsampling
 *   for a good balance between size and color fidelity.
 */

#ifndef GI_JPEG_H
#define GI_JPEG_H

#include "gi_image.h"

/** @brief Start of Image marker. */
#define GI_JPEG_SOI 0xD8
/** @brief End of Image marker. */
#define GI_JPEG_EOI 0xD9
/** @brief APP0 — JFIF header. */
#define GI_JPEG_APP0 0xE0
/** @brief APP1 — EXIF / XMP metadata. */
#define GI_JPEG_APP1 0xE1
/** @brief APP2 — ICC color profile (always preserved). */
#define GI_JPEG_APP2 0xE2
/** @brief COM — text comment segment. */
#define GI_JPEG_COM 0xFE
/** @brief SOS — start of compressed scan data. */
#define GI_JPEG_SOS 0xDA

/**
 * @brief Collects metadata from a JPEG file into a @ref GiImageInfo struct.
 *
 * Walks all markers and extracts EXIF fields (make, model, datetime,
 * software, GPS coordinates) without modifying the file.
 *
 * @param path Path to the source JPEG file.
 * @param info Output struct to populate.
 * @return 0 on success, -1 on error.
 */
int gi_jpeg_get_info(const char *path, GiImageInfo *info);

/**
 * @brief Cleans or optimizes a JPEG file.
 *
 * Dispatches to the lossless or lossy path based on @p args->opt_mode.
 * Never prints anything — all output data is written into @p result.
 *
 * @param src    Path to the source file.
 * @param dst    Path to the destination file (may equal @p src for in-place).
 * @param opt    Processing strategy (@ref GiOptMode).
 * @param quality Re-encode quality 1-100, used only when @p opt is
 *               @c GI_OPT_LOSSY. Recommended default: 85.
 * @param dry_run If 1, compute result without writing any file.
 * @param result  Output struct to populate with the operation result.
 */
void gi_jpeg_clean(const char *src, const char *dst, GiOptMode opt, int quality,
                   int dry_run, GiCleanResult *result);

#endif /* GI_JPEG_H */
