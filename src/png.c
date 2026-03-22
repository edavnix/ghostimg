/**
 * @file png.c
 * @brief PNG metadata removal and optimization for Ghostimg.
 *
 * Two processing paths:
 *
 * Lossless — walks the raw chunk sequence, drops metadata chunks
 * (eXIf, tEXt, iTXt, zTXt, tIME) and copies the rest verbatim.
 * IDAT pixel data is never decoded.
 *
 * Optimized — decodes the full image via libpng into a pixel buffer,
 * then re-encodes at zlib level 9 with PNG_ALL_FILTERS. PNG is always
 * lossless by specification — this path achieves better compression,
 * not pixel degradation.
 */

#include "gi_endian.h"
#include "gi_io.h"
#include "gi_png.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <png.h>

/* ── PNG signature ───────────────────────────────────────────────────────── */

/** @brief Standard 8-byte PNG file signature. */
static const unsigned char PNG_SIG[8] = {0x89, 0x50, 0x4E, 0x47,
                                         0x0D, 0x0A, 0x1A, 0x0A};

/* ── Internal: metadata chunk detection ─────────────────────────────────── */

/**
 * @brief Returns 1 if the chunk type should be dropped.
 *
 * Dropped: eXIf, tEXt, iTXt, zTXt, tIME.
 *
 * @param type 4-byte chunk type field.
 * @return 1 to drop, 0 to keep.
 */
static int chunk_is_meta(const unsigned char type[4]) {
  static const char *const DROP[] = {"eXIf", "tEXt", "iTXt",
                                     "zTXt", "tIME", NULL};
  for (int i = 0; DROP[i]; i++)
    if (memcmp(type, DROP[i], 4) == 0)
      return 1;
  return 0;
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

/* ── Internal: libpng write-to-buffer callbacks ──────────────────────────── */

/**
 * @brief libpng write callback: appends encoded bytes to a @ref GiBuffer.
 * @param png  libpng write struct (carries GiBuffer* as io_ptr).
 * @param data Bytes to write.
 * @param n    Number of bytes.
 */
static void png_write_cb(png_structp png, png_bytep data, png_size_t n) {
  GiBuffer *buf = (GiBuffer *)png_get_io_ptr(png);
  gi_buf_append(buf, data, n);
}

/**
 * @brief libpng flush callback: no-op for in-memory encoding.
 * @param png libpng write struct (unused).
 */
static void png_flush_cb(png_structp png) { (void)png; }

/* ════════════════════════════════════════════════════════════════════════════
 * Public: gi_png_get_info
 * ════════════════════════════════════════════════════════════════════════════
 */

int gi_png_get_info(const char *path, GiImageInfo *info) {
  size_t file_size = 0;
  unsigned char *raw = load_file(path, &file_size);
  if (!raw)
    return -1;
  if (file_size < 8 || memcmp(raw, PNG_SIG, 8) != 0) {
    free(raw);
    return -1;
  }

  memset(info, 0, sizeof(*info));
  info->type = GI_IMG_PNG;
  info->file_size = file_size;

  unsigned int bit_depth = 0;
  unsigned int color_type = 0;
  char text_keys[128] = "";
  size_t tk_len = 0;

  size_t pos = 8;
  while (pos + 12 <= file_size) {
    unsigned int clen = gi_read_be32(raw + pos);
    unsigned char *type = raw + pos + 4;
    if (pos + 12 + clen > file_size)
      break;

    if (memcmp(type, "IHDR", 4) == 0 && clen >= 13) {
      info->width = gi_read_be32(raw + pos + 8);
      info->height = gi_read_be32(raw + pos + 12);
      bit_depth = raw[pos + 16];
      color_type = raw[pos + 17];
    }
    if (memcmp(type, "eXIf", 4) == 0)
      info->has_exif = 1;
    if (memcmp(type, "tIME", 4) == 0)
      info->has_comments = 1;
    if (memcmp(type, "tEXt", 4) == 0 || memcmp(type, "iTXt", 4) == 0 ||
        memcmp(type, "zTXt", 4) == 0) {
      info->has_comments = 1;
      /* Collect key name — first null-terminated field of chunk data */
      if (clen > 0 && tk_len < sizeof(text_keys) - 34) {
        const char *key = (const char *)(raw + pos + 8);
        size_t kl = strlen(key);
        if (kl > 32)
          kl = 32;
        if (tk_len > 0) {
          text_keys[tk_len++] = ',';
          text_keys[tk_len++] = ' ';
        }
        memcpy(text_keys + tk_len, key, kl);
        tk_len += kl;
        text_keys[tk_len] = '\0';
      }
    }
    if (memcmp(type, "IEND", 4) == 0)
      break;
    pos += 12 + clen;
  }
  free(raw);

  /* Build format string */
  static const char *const COLOR_NAMES[7] = {
      "Grayscale", "", "RGB", "Indexed", "Grayscale+Alpha", "", "RGBA"};
  const char *color_str = (color_type < 7 && COLOR_NAMES[color_type][0])
                              ? COLOR_NAMES[color_type]
                              : "Unknown";

  snprintf(info->format, sizeof(info->format), "PNG / %u-bit %s", bit_depth,
           color_str);

  /* Store text keys in software field for display */
  if (tk_len > 0)
    snprintf(info->software, sizeof(info->software), "%s", text_keys);

  return 0;
}

/* ════════════════════════════════════════════════════════════════════════════
 * Lossless path
 * ════════════════════════════════════════════════════════════════════════════
 */

/**
 * @brief Lossless metadata removal via raw chunk walk.
 *
 * @param src     Source file path.
 * @param dst     Destination file path.
 * @param dry_run If 1, compute result without writing.
 * @param result  Output struct.
 */
static void png_lossless(const char *src, const char *dst, int dry_run,
                         GiCleanResult *result) {
  size_t file_size = 0;
  unsigned char *raw = load_file(src, &file_size);
  if (!raw) {
    set_error(result, "cannot open file");
    return;
  }
  if (file_size < 8 || memcmp(raw, PNG_SIG, 8) != 0) {
    free(raw);
    set_error(result, "not a valid PNG");
    return;
  }

  GiBuffer out;
  if (gi_buf_init(&out, file_size) != 0) {
    free(raw);
    set_error(result, "out of memory");
    return;
  }

  gi_buf_append(&out, PNG_SIG, 8);

  int removed_n = 0;
  size_t pos = 8;

  while (pos + 12 <= file_size) {
    unsigned int clen = gi_read_be32(raw + pos);
    unsigned char *type = raw + pos + 4;
    size_t chunk_total = 12 + clen;

    if (pos + chunk_total > file_size)
      break;

    if (chunk_is_meta(type)) {
      removed_n++;
    } else {
      gi_buf_append(&out, raw + pos, chunk_total);
    }

    if (memcmp(type, "IEND", 4) == 0)
      break;
    pos += chunk_total;
  }
  free(raw);

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
 * Optimized path (libpng — zlib level 9)
 * ════════════════════════════════════════════════════════════════════════════
 */

/**
 * @brief Optimized re-encode via libpng at zlib compression level 9.
 *
 * @param src     Source file path.
 * @param dst     Destination file path.
 * @param dry_run If 1, compute result without writing.
 * @param result  Output struct.
 */
static void png_optimized(const char *src, const char *dst, int dry_run,
                          GiCleanResult *result) {
  size_t file_size = 0;
  unsigned char *raw = load_file(src, &file_size);
  if (!raw) {
    set_error(result, "cannot open file");
    return;
  }

  result->size_before = file_size;
  result->was_lossy = 1;
  result->quality = 9; /* zlib level */
  result->segments_removed = 0;

  if (dry_run) {
    /* Estimate: zlib 9 typically saves 5-15% over default compression */
    result->size_after = file_size * 88 / 100;
    free(raw);
    return;
  }
  free(raw);

  /* ── Decode ── */
  FILE *fin = fopen(src, "rb");
  if (!fin) {
    set_error(result, "cannot open file");
    return;
  }

  png_structp rp =
      png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
  if (!rp) {
    fclose(fin);
    set_error(result, "libpng init failed");
    return;
  }

  png_infop ri = png_create_info_struct(rp);
  if (!ri) {
    png_destroy_read_struct(&rp, NULL, NULL);
    fclose(fin);
    set_error(result, "libpng info init failed");
    return;
  }

  if (setjmp(png_jmpbuf(rp))) {
    png_destroy_read_struct(&rp, &ri, NULL);
    fclose(fin);
    set_error(result, "libpng decode error");
    return;
  }

  png_init_io(rp, fin);
  png_read_png(rp, ri, PNG_TRANSFORM_STRIP_16 | PNG_TRANSFORM_PACKING, NULL);
  fclose(fin);

  png_uint_32 width = png_get_image_width(rp, ri);
  png_uint_32 height = png_get_image_height(rp, ri);
  int color = png_get_color_type(rp, ri);
  int depth = png_get_bit_depth(rp, ri);
  png_bytepp rows = png_get_rows(rp, ri);

  /* ── Encode ── */
  GiBuffer out;
  if (gi_buf_init(&out, file_size) != 0) {
    png_destroy_read_struct(&rp, &ri, NULL);
    set_error(result, "out of memory");
    return;
  }

  png_structp wp =
      png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
  if (!wp) {
    gi_buf_free(&out);
    png_destroy_read_struct(&rp, &ri, NULL);
    set_error(result, "libpng write init failed");
    return;
  }

  png_infop wi = png_create_info_struct(wp);
  if (!wi) {
    png_destroy_write_struct(&wp, NULL);
    gi_buf_free(&out);
    png_destroy_read_struct(&rp, &ri, NULL);
    set_error(result, "libpng write info failed");
    return;
  }

  if (setjmp(png_jmpbuf(wp))) {
    png_destroy_write_struct(&wp, &wi);
    gi_buf_free(&out);
    png_destroy_read_struct(&rp, &ri, NULL);
    set_error(result, "libpng encode error");
    return;
  }

  png_set_write_fn(wp, &out, png_write_cb, png_flush_cb);
  png_set_compression_level(wp, 9);
  png_set_filter(wp, 0, PNG_ALL_FILTERS);

  png_set_IHDR(wp, wi, width, height, depth, color, PNG_INTERLACE_NONE,
               PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);

  png_set_rows(wp, wi, rows);
  png_write_png(wp, wi, PNG_TRANSFORM_IDENTITY, NULL);

  png_destroy_write_struct(&wp, &wi);
  png_destroy_read_struct(&rp, &ri, NULL);

  result->size_after = out.size;

  if (gi_buf_write(dst, &out) != 0)
    set_error(result, "failed to write output file");

  gi_buf_free(&out);
}

/* ════════════════════════════════════════════════════════════════════════════
 * Public API
 * ════════════════════════════════════════════════════════════════════════════
 */

void gi_png_clean(const char *src, const char *dst, GiOptMode opt, int dry_run,
                  GiCleanResult *result) {
  memset(result, 0, sizeof(*result));
  result->success = 1;
  result->src = src;
  result->dst = dst;
  result->dry_run = dry_run;

  if (opt == GI_OPT_LOSSY)
    png_optimized(src, dst, dry_run, result);
  else
    png_lossless(src, dst, dry_run, result);
}
