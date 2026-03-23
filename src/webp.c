/**
 * @file webp.c
 * @brief WebP metadata removal and optimization for Ghostimg.
 *
 * Two processing paths:
 *
 * Lossless — rebuilds the RIFF container without EXIF, XMP and ICCP
 * chunks. The VP8 / VP8L / VP8X bitstream is copied verbatim.
 * The RIFF size field at bytes 4-7 is patched to reflect the new size.
 *
 * Lossy — decodes the full image via libwebp into an RGB pixel buffer,
 * then re-encodes at the target quality. At quality 100 the lossless
 * WebP encoder is used for maximum fidelity. All metadata is excluded
 * from the new stream by construction.
 */

#include "gi_endian.h"
#include "gi_io.h"
#include "gi_webp.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <webp/decode.h>
#include <webp/encode.h>

/* ── RIFF / WebP magic constants ─────────────────────────────────────────── */

static const unsigned char RIFF_SIG[4] = {'R', 'I', 'F', 'F'};
static const unsigned char WEBP_SIG[4] = {'W', 'E', 'B', 'P'};

/* ── Internal: metadata chunk detection ─────────────────────────────────── */

/**
 * @brief Returns 1 if the chunk FourCC should be dropped.
 *
 * Dropped: EXIF, XMP (stored as "XMP "), ICCP.
 *
 * @param fcc 4-byte chunk FourCC.
 * @return 1 to drop, 0 to keep.
 */
static int chunk_is_meta(const unsigned char fcc[4]) {
  return memcmp(fcc, "EXIF", 4) == 0 || memcmp(fcc, "XMP ", 4) == 0 ||
         memcmp(fcc, "ICCP", 4) == 0;
}

/* ── Internal: file loader ───────────────────────────────────────────────── */

/**
 * @brief Loads an entire file into a heap-allocated buffer.
 *
 * @param path Path to the file.
 * @param sz   Output: number of bytes read.
 * @return Heap-allocated buffer, or NULL on error. Caller must free.
 */
static unsigned char *load_file(const char *path, size_t *sz) {
  FILE *f = fopen(path, "rb");
  if (!f)
    return NULL;

  fseek(f, 0, SEEK_END);
  long len = ftell(f);
  rewind(f);
  if (len <= 0) {
    fclose(f);
    return NULL;
  }

  unsigned char *buf = (unsigned char *)malloc((size_t)len);
  if (!buf) {
    fclose(f);
    return NULL;
  }

  if (fread(buf, 1, (size_t)len, f) != (size_t)len) {
    free(buf);
    fclose(f);
    return NULL;
  }
  fclose(f);
  *sz = (size_t)len;
  return buf;
}

/* ── Internal: RIFF validation ───────────────────────────────────────────── */

/**
 * @brief Returns 1 if the buffer starts with a valid RIFF/WEBP header.
 * @param buf File data.
 * @param sz  File size.
 * @return 1 if valid, 0 otherwise.
 */
static int is_webp(const unsigned char *buf, size_t sz) {
  return sz >= 12 && memcmp(buf, RIFF_SIG, 4) == 0 &&
         memcmp(buf + 8, WEBP_SIG, 4) == 0;
}

/* ── Internal: result helpers ────────────────────────────────────────────── */

/**
 * @brief Fills a @ref GiCleanResult with an error message.
 * @param result Output struct.
 * @param msg    Error description.
 */
static void set_error(GiCleanResult *result, const char *msg) {
  result->success = 0;
  strncpy(result->error_msg, msg, sizeof(result->error_msg) - 1);
  result->error_msg[sizeof(result->error_msg) - 1] = '\0';
}

/* ════════════════════════════════════════════════════════════════════════════
 * Public: gi_webp_get_info
 * ════════════════════════════════════════════════════════════════════════════
 */

int gi_webp_get_info(const char *path, GiImageInfo *info) {
  size_t file_size = 0;
  unsigned char *raw = load_file(path, &file_size);
  if (!raw)
    return -1;
  if (!is_webp(raw, file_size)) {
    free(raw);
    return -1;
  }

  memset(info, 0, sizeof(*info));
  info->type = GI_IMG_WEBP;
  info->file_size = file_size;

  /* Use libwebp to extract dimensions */
  WebPBitstreamFeatures feat;
  if (WebPGetFeatures(raw, file_size, &feat) == VP8_STATUS_OK) {
    info->width = (unsigned int)feat.width;
    info->height = (unsigned int)feat.height;
  }

  /* Walk RIFF chunks for metadata presence and subtype */
  const char *subtype = "VP8 (lossy)";
  size_t pos = 12;

  while (pos + 8 <= file_size) {
    unsigned int clen = gi_read_le32(raw + pos + 4);
    unsigned char *fcc = raw + pos;
    size_t padded = clen + (clen & 1u);

    if (memcmp(fcc, "EXIF", 4) == 0)
      info->has_exif = 1;
    if (memcmp(fcc, "XMP ", 4) == 0)
      info->has_xmp = 1;
    if (memcmp(fcc, "ICCP", 4) == 0)
      info->has_icc = 1;
    if (memcmp(fcc, "VP8L", 4) == 0)
      subtype = "VP8L (lossless)";
    if (memcmp(fcc, "VP8X", 4) == 0)
      subtype = "VP8X (extended)";

    if (pos + 8 + padded > file_size)
      break;
    pos += 8 + padded;
  }
  free(raw);

  snprintf(info->format, sizeof(info->format), "WebP / %s", subtype);
  return 0;
}

/* ════════════════════════════════════════════════════════════════════════════
 * Lossless path — RIFF chunk walk
 * ════════════════════════════════════════════════════════════════════════════
 */

/**
 * @brief Lossless metadata removal via raw RIFF chunk walk.
 *
 * Rebuilds the RIFF container without metadata chunks and patches
 * the RIFF size field to reflect the new payload size.
 *
 * @param src     Source file path.
 * @param dst     Destination file path.
 * @param dry_run If 1, compute result without writing.
 * @param result  Output struct.
 */
static void webp_lossless(const char *src, const char *dst, int dry_run,
                          GiCleanResult *result) {
  size_t file_size = 0;
  unsigned char *raw = load_file(src, &file_size);
  if (!raw) {
    set_error(result, "cannot open file");
    return;
  }
  if (!is_webp(raw, file_size)) {
    free(raw);
    set_error(result, "not a valid WebP");
    return;
  }

  GiBuffer out;
  if (gi_buf_init(&out, file_size) != 0) {
    free(raw);
    set_error(result, "out of memory");
    return;
  }

  /* Write RIFF header — size field patched after all chunks are written */
  gi_buf_append(&out, raw, 12);

  int removed_n = 0;
  size_t pos = 12;

  while (pos + 8 <= file_size) {
    unsigned int clen = gi_read_le32(raw + pos + 4);
    unsigned char *fcc = raw + pos;
    size_t padded = clen + (clen & 1u);
    size_t chunk_total = 8 + padded;

    if (pos + chunk_total > file_size)
      break;

    if (chunk_is_meta(fcc)) {
      removed_n++;
    } else {
      gi_buf_append(&out, raw + pos, chunk_total);
    }

    pos += chunk_total;
  }
  free(raw);

  /* Patch RIFF size: total file size minus the 8-byte RIFF header */
  gi_write_le32(out.data + 4, (unsigned int)(out.size - 8));

  result->size_before = file_size;
  result->size_after = out.size;
  result->segments_removed = removed_n;
  result->was_lossy = 0;
  result->quality = 0;

  if (!dry_run) {
    if (gi_buf_write(dst, &out) != 0)
      set_error(result, "failed to write output file");
  }

  gi_buf_free(&out);
}

/* ════════════════════════════════════════════════════════════════════════════
 * Lossy path — libwebp decode + re-encode
 * ════════════════════════════════════════════════════════════════════════════
 */

/**
 * @brief Lossy re-encode via libwebp.
 *
 * At quality 100 uses the lossless WebP encoder (WebPEncodeLosslessRGB)
 * for maximum fidelity. Below 100 uses the lossy encoder (WebPEncodeRGB).
 *
 * @param src     Source file path.
 * @param dst     Destination file path.
 * @param quality Re-encode quality 1-100.
 * @param dry_run If 1, compute result without writing.
 * @param result  Output struct.
 */
static void webp_lossy(const char *src, const char *dst, int quality,
                       int dry_run, GiCleanResult *result) {
  size_t file_size = 0;
  unsigned char *raw = load_file(src, &file_size);
  if (!raw) {
    set_error(result, "cannot open file");
    return;
  }
  if (!is_webp(raw, file_size)) {
    free(raw);
    set_error(result, "not a valid WebP");
    return;
  }

  result->size_before = file_size;
  result->was_lossy = 1;
  result->quality = quality;
  result->segments_removed = 0;

  if (dry_run) {
    /* Estimate: WebP lossy is very efficient — ~30% of original */
    result->size_after = file_size * 30 / 100;
    free(raw);
    return;
  }

  int width = 0, height = 0;
  unsigned char *pixels = WebPDecodeRGB(raw, file_size, &width, &height);
  free(raw);

  if (!pixels) {
    set_error(result, "WebP decode failed");
    return;
  }

  uint8_t *enc_buf = NULL;
  size_t enc_size = 0;

  if (quality >= 100)
    enc_size =
        WebPEncodeLosslessRGB(pixels, width, height, width * 3, &enc_buf);
  else
    enc_size = (size_t)WebPEncodeRGB(pixels, width, height, width * 3,
                                     (float)quality, &enc_buf);
  WebPFree(pixels);

  if (!enc_buf || enc_size == 0) {
    set_error(result, "WebP encode failed");
    return;
  }

  result->size_after = enc_size;

  /* Wrap libwebp output in GiBuffer to reuse gi_buf_write */
  GiBuffer wrapped;
  wrapped.data = enc_buf;
  wrapped.size = enc_size;
  wrapped.cap = enc_size;

  if (gi_buf_write(dst, &wrapped) != 0)
    set_error(result, "failed to write output file");

  WebPFree(enc_buf);
}

/* ════════════════════════════════════════════════════════════════════════════
 * Public API
 * ════════════════════════════════════════════════════════════════════════════
 */

void gi_webp_clean(const char *src, const char *dst, GiOptMode opt, int quality,
                   int dry_run, GiCleanResult *result) {
  memset(result, 0, sizeof(*result));
  result->success = 1;
  result->src = src;
  result->dst = dst;
  result->dry_run = dry_run;
  result->quality = quality;

  if (opt == GI_OPT_LOSSY)
    webp_lossy(src, dst, quality, dry_run, result);
  else
    webp_lossless(src, dst, dry_run, result);
}
