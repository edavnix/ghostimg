/**
 * @file gi_png.h
 * @brief PNG metadata removal and optimization interface.
 *
 * This module handles chunk-based parsing of PNG files to strip
 * metadata and optionally re-encode with maximum zlib compression.
 *
 * All functions return pure data structs — no output is printed here.
 * The caller (batch.c) passes results to the UI layer for display.
 *
 * Lossless path: walks raw PNG chunks, drops metadata chunks only.
 *   IDAT pixel data is never decoded or modified.
 *
 * Optimized path: full decode via libpng, re-encode at zlib level 9
 *   with adaptive filter selection. PNG is always lossless by spec —
 *   this path achieves better compression, not pixel degradation.
 *
 * Chunks dropped: eXIf, tEXt, iTXt, zTXt, tIME.
 * Chunks always kept: IHDR, PLTE, IDAT, IEND, cHRM, gAMA, sRGB,
 *                     iCCP, bKGD, pHYs, sBIT, sPLT, hIST, tRNS.
 */

#ifndef GI_PNG_H
#define GI_PNG_H

#include "gi_image.h"

/**
 * @brief Collects metadata from a PNG file into a @ref GiImageInfo struct.
 *
 * Walks all chunks and reports embedded metadata types without
 * modifying the file.
 *
 * @param path Path to the source PNG file.
 * @param info Output struct to populate.
 * @return 0 on success, -1 on error.
 */
int gi_png_get_info(const char *path, GiImageInfo *info);

/**
 * @brief Cleans or optimizes a PNG file.
 *
 * Never prints anything — all output data is written into @p result.
 *
 * Lossless: removes metadata chunks, IDAT untouched.
 * Lossy (optimized): re-encodes at zlib level 9 — no pixel loss.
 *
 * @param src     Path to the source file.
 * @param dst     Path to the destination file (may equal @p src for in-place).
 * @param opt     Processing strategy (@ref GiOptMode).
 * @param dry_run If 1, compute result without writing any file.
 * @param result  Output struct to populate with the operation result.
 */
void gi_png_clean(const char *src, const char *dst, GiOptMode opt, int dry_run,
                  GiCleanResult *result);

#endif /* GI_PNG_H */
