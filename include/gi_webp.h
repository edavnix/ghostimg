/**
 * @file gi_webp.h
 * @brief WebP metadata removal and optimization interface.
 *
 * This module handles chunk-based parsing of WebP RIFF containers to
 * strip metadata and optionally re-encode image data via libwebp.
 *
 * All functions return pure data structs — no output is printed here.
 * The caller (batch.c) passes results to the UI layer for display.
 *
 * Lossless path: rebuilds the RIFF container without EXIF, XMP and
 *   ICCP chunks. The VP8/VP8L/VP8X bitstream is copied verbatim.
 *   The RIFF size field is patched to reflect the new payload size.
 *
 * Lossy path: full decode via libwebp into an RGB pixel buffer, then
 *   re-encodes at target quality. At quality 100 the lossless WebP
 *   encoder is used for best results. All metadata is excluded from
 *   the new stream by construction.
 *
 * Chunks dropped: EXIF, XMP (stored as "XMP "), ICCP.
 * Chunks always kept: VP8, VP8L, VP8X, ANIM, ANMF, ALPH.
 */

#ifndef GI_WEBP_H
#define GI_WEBP_H

#include "gi_image.h"

/**
 * @brief Collects metadata from a WebP file into a @ref GiImageInfo struct.
 *
 * Inspects the RIFF container and reports format subtype (VP8/VP8L/VP8X),
 * dimensions, and embedded metadata chunks without modifying the file.
 *
 * @param path Path to the source WebP file.
 * @param info Output struct to populate.
 * @return 0 on success, -1 on error.
 */
int gi_webp_get_info(const char *path, GiImageInfo *info);

/**
 * @brief Cleans or optimizes a WebP file.
 *
 * Never prints anything — all output data is written into @p result.
 *
 * Lossless: removes EXIF/XMP/ICCP chunks, bitstream untouched.
 * Lossy: decodes and re-encodes at @p quality via libwebp.
 *
 * @param src     Path to the source file.
 * @param dst     Path to the destination file (may equal @p src for in-place).
 * @param opt     Processing strategy (@ref GiOptMode).
 * @param quality Re-encode quality 1-100, used only when @p opt is
 *                @c GI_OPT_LOSSY. At 100 uses lossless WebP encoder.
 * @param dry_run If 1, compute result without writing any file.
 * @param result  Output struct to populate with the operation result.
 */
void gi_webp_clean(const char *src, const char *dst, GiOptMode opt, int quality,
                   int dry_run, GiCleanResult *result);

#endif /* GI_WEBP_H */
