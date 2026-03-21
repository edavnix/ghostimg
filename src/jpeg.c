/**
 * @file jpeg.c
 * @brief JPEG metadata removal and optimization for Ghostimg.
 *
 * Two processing paths:
 *
 * Lossless — walks raw JPEG markers sequentially, drops non-image
 * segments (APP0, APP1, APP3-APPF, COM) and copies the rest verbatim.
 * The compressed scan data after SOS is never decoded.
 *
 * Lossy — decodes the full image via libjpeg-turbo into an RGB pixel
 * buffer, then re-encodes at the target quality using 4:2:2 chroma
 * subsampling and progressive encoding for web optimization.
 * All metadata is excluded from the new stream by construction.
 */

#include "gi_endian.h"
#include "gi_io.h"
#include "gi_jpeg.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <jpeglib.h>

/* ── TIFF / EXIF constants ───────────────────────────────────────────────── */

/** @brief TIFF tag for GPS SubIFD offset. */
#define TIFF_TAG_GPS 0x8825

/* ── Internal: TIFF context ──────────────────────────────────────────────── */

/**
 * @brief Carries a TIFF data block and its byte-order flag.
 */
typedef struct {
  const unsigned char *data; /**< Pointer to start of TIFF block. */
  size_t size;               /**< Size of the TIFF block in bytes. */
  int le;                    /**< 1 = little-endian (Intel), 0 = big-endian. */
} TiffCtx;

/**
 * @brief Reads a 16-bit value from the TIFF block respecting byte order.
 * @param c   TIFF context.
 * @param off Byte offset within the block.
 * @return 16-bit value, or 0 if offset is out of bounds.
 */
static unsigned short tiff_u16(const TiffCtx *c, unsigned int off) {
  if ((size_t)off + 2 > c->size)
    return 0;
  return c->le ? gi_read_le16(c->data + off) : gi_read_be16(c->data + off);
}

/**
 * @brief Reads a 32-bit value from the TIFF block respecting byte order.
 * @param c   TIFF context.
 * @param off Byte offset within the block.
 * @return 32-bit value, or 0 if offset is out of bounds.
 */
static unsigned int tiff_u32(const TiffCtx *c, unsigned int off) {
  if ((size_t)off + 4 > c->size)
    return 0;
  return c->le ? gi_read_le32(c->data + off) : gi_read_be32(c->data + off);
}

/* ── Internal: TIFF tag helpers ──────────────────────────────────────────── */

/** @brief Byte sizes for TIFF data types (indexed by type code 1-5). */
static const unsigned int TIFF_TYPE_SIZE[6] = {0, 1, 1, 2, 4, 8};

/**
 * @brief Returns the value offset for a TIFF IFD entry.
 *
 * If the value fits in 4 bytes it is stored inline at entry+8;
 * otherwise entry+8 holds an offset into the TIFF block.
 *
 * @param c    TIFF context.
 * @param eoff Byte offset of the IFD entry (12-byte record).
 * @param type TIFF data type code.
 * @param cnt  Number of values.
 * @return Byte offset of the actual value data.
 */
static unsigned int tiff_val_off(const TiffCtx *c, unsigned int eoff,
                                 unsigned short type, unsigned int cnt) {
  unsigned int unit = (type < 6) ? TIFF_TYPE_SIZE[type] : 1;
  return (unit * cnt <= 4) ? eoff + 8 : tiff_u32(c, eoff + 8);
}

/**
 * @brief Copies a null-terminated ASCII TIFF value into @p buf.
 * @param c   TIFF context.
 * @param off Value offset within the block.
 * @param cnt Byte count including null terminator.
 * @param buf Destination buffer.
 * @param sz  Size of the destination buffer.
 */
static void tiff_ascii(const TiffCtx *c, unsigned int off, unsigned int cnt,
                       char *buf, size_t sz) {
  size_t n = (cnt < sz) ? cnt : sz - 1;
  if ((size_t)off + n > c->size) {
    buf[0] = '\0';
    return;
  }
  memcpy(buf, c->data + off, n);
  buf[n] = '\0';
}

/* ── Internal: GPS DMS → decimal ─────────────────────────────────────────── */

/**
 * @brief Converts a GPS DMS rational triplet to decimal degrees.
 *
 * Each component is stored as a TIFF RATIONAL (two 32-bit integers).
 *
 * @param c   TIFF context.
 * @param off Offset of the first rational value.
 * @param cnt Number of rational values (must be >= 3).
 * @return Decimal degrees, or 0.0 on invalid input.
 */
static double gps_to_decimal(const TiffCtx *c, unsigned int off,
                             unsigned int cnt) {
  if (cnt < 3)
    return 0.0;
  double deg = (double)tiff_u32(c, off) / (double)tiff_u32(c, off + 4);
  double min = (double)tiff_u32(c, off + 8) / (double)tiff_u32(c, off + 12);
  double sec = (double)tiff_u32(c, off + 16) / (double)tiff_u32(c, off + 20);
  return deg + min / 60.0 + sec / 3600.0;
}

/* ── Internal: IFD walkers ───────────────────────────────────────────────── */

/**
 * @brief Walks the GPS SubIFD and fills GPS fields in @p info.
 * @param c    TIFF context.
 * @param off  Offset of the GPS IFD.
 * @param info Output struct.
 */
static void walk_gps_ifd(const TiffCtx *c, unsigned int off,
                         GiImageInfo *info) {
  if ((size_t)off + 2 > c->size)
    return;
  unsigned short n = tiff_u16(c, off);
  off += 2;

  for (unsigned short i = 0; i < n; i++, off += 12) {
    if ((size_t)off + 12 > c->size)
      break;
    unsigned short tag = tiff_u16(c, off);
    unsigned short type = tiff_u16(c, off + 2);
    unsigned int cnt = tiff_u32(c, off + 4);
    unsigned int vo = tiff_val_off(c, off, type, cnt);

    switch (tag) {
    case 0x0001:
      tiff_ascii(c, vo, cnt, info->gps_lat_ref, sizeof(info->gps_lat_ref));
      break;
    case 0x0002:
      info->gps_lat = gps_to_decimal(c, vo, cnt);
      info->has_gps = 1;
      break;
    case 0x0003:
      tiff_ascii(c, vo, cnt, info->gps_lon_ref, sizeof(info->gps_lon_ref));
      break;
    case 0x0004:
      info->gps_lon = gps_to_decimal(c, vo, cnt);
      break;
    default:
      break;
    }
  }
}

/**
 * @brief Walks IFD0 and fills all supported fields in @p info.
 * @param c    TIFF context.
 * @param off  Offset of IFD0.
 * @param info Output struct.
 */
static void walk_ifd0(const TiffCtx *c, unsigned int off, GiImageInfo *info) {
  if ((size_t)off + 2 > c->size)
    return;
  unsigned short n = tiff_u16(c, off);
  off += 2;

  for (unsigned short i = 0; i < n; i++, off += 12) {
    if ((size_t)off + 12 > c->size)
      break;
    unsigned short tag = tiff_u16(c, off);
    unsigned short type = tiff_u16(c, off + 2);
    unsigned int cnt = tiff_u32(c, off + 4);
    unsigned int vo = tiff_val_off(c, off, type, cnt);

    switch (tag) {
    case 0x010F:
      tiff_ascii(c, vo, cnt, info->make, sizeof(info->make));
      break;
    case 0x0110:
      tiff_ascii(c, vo, cnt, info->model, sizeof(info->model));
      break;
    case 0x0132:
      tiff_ascii(c, vo, cnt, info->datetime, sizeof(info->datetime));
      break;
    case 0x0131:
      tiff_ascii(c, vo, cnt, info->software, sizeof(info->software));
      break;
    case 0x0100:
      info->width = (type == 3) ? tiff_u16(c, vo) : tiff_u32(c, vo);
      break;
    case 0x0101:
      info->height = (type == 3) ? tiff_u16(c, vo) : tiff_u32(c, vo);
      break;
    case TIFF_TAG_GPS:
      walk_gps_ifd(c, tiff_u32(c, off + 8), info);
      break;
    default:
      break;
    }
  }
}

/* ── Internal: EXIF / APP1 parser ────────────────────────────────────────── */

/**
 * @brief Parses an APP1 EXIF segment and fills @p info.
 *
 * @param seg Pointer to segment data (after the 4-byte marker+length).
 * @param len Length of the segment data.
 * @param info Output struct.
 * @return 0 if EXIF was found and parsed, -1 otherwise.
 */
static int parse_exif(const unsigned char *seg, size_t len, GiImageInfo *info) {
  if (len < 6 || memcmp(seg, "Exif\0\0", 6) != 0)
    return -1;

  TiffCtx c;
  c.data = seg + 6;
  c.size = len - 6;
  if (c.size < 8)
    return -1;

  if (c.data[0] == 'I' && c.data[1] == 'I')
    c.le = 1;
  else if (c.data[0] == 'M' && c.data[1] == 'M')
    c.le = 0;
  else
    return -1;

  walk_ifd0(&c, tiff_u32(&c, 4), info);
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

/* ── Internal: segment drop decision ────────────────────────────────────── */

/**
 * @brief Returns 1 if the marker should be dropped.
 *
 * Dropped: APP0 (JFIF), APP1 (EXIF/XMP), APP3-APPF (vendor), COM.
 * Kept:    APP2 (ICC), DQT, DHT, SOF*, SOS, EOI and all image data.
 *
 * @param marker Second byte of the FF-prefixed marker.
 * @return 1 to drop, 0 to keep.
 */
static int should_drop(unsigned char marker) {
  if (marker == GI_JPEG_APP0)
    return 1;
  if (marker == GI_JPEG_APP1)
    return 1;
  if (marker == GI_JPEG_COM)
    return 1;
  if (marker >= 0xE3 && marker <= 0xEF)
    return 1; /* APP3-APPF */
  return 0;
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

/**
 * @brief Returns the file size in bytes, or 0 on error.
 * @param path File path.
 * @return File size in bytes.
 */
static size_t file_size_of(const char *path) {
  FILE *f = fopen(path, "rb");
  if (!f)
    return 0;
  fseek(f, 0, SEEK_END);
  long sz = ftell(f);
  fclose(f);
  return sz > 0 ? (size_t)sz : 0;
}

/* ════════════════════════════════════════════════════════════════════════════
 * Lossless path
 * ════════════════════════════════════════════════════════════════════════════
 */

/**
 * @brief Lossless metadata removal via raw marker walk.
 *
 * @param src      Source file path.
 * @param dst      Destination file path.
 * @param dry_run  If 1, compute result without writing.
 * @param result   Output struct.
 */
static void jpeg_lossless(const char *src, const char *dst, int dry_run,
                          GiCleanResult *result) {
  size_t file_size = 0;
  unsigned char *raw = load_file(src, &file_size);
  if (!raw) {
    set_error(result, "cannot open file");
    return;
  }
  if (file_size < 2 || raw[0] != 0xFF || raw[1] != GI_JPEG_SOI) {
    free(raw);
    set_error(result, "not a valid JPEG");
    return;
  }

  GiBuffer out;
  if (gi_buf_init(&out, file_size) != 0) {
    free(raw);
    set_error(result, "out of memory");
    return;
  }

  gi_buf_append(&out, raw, 2); /* SOI */

  int removed_n = 0;
  size_t pos = 2;

  while (pos + 3 < file_size) {
    if (raw[pos] != 0xFF) {
      gi_buf_append(&out, raw + pos, file_size - pos);
      break;
    }

    unsigned char m = raw[pos + 1];

    if (m == GI_JPEG_EOI) {
      gi_buf_append(&out, raw + pos, 2);
      break;
    }
    if (m == GI_JPEG_SOI) {
      gi_buf_append(&out, raw + pos, 2);
      pos += 2;
      continue;
    }
    if (m == 0xFF) {
      pos++;
      continue;
    }

    if (pos + 3 >= file_size)
      break;
    unsigned short slen = gi_read_be16(raw + pos + 2);
    if (slen < 2)
      break;
    size_t seg_total = 2 + (size_t)slen;

    if (should_drop(m)) {
      removed_n++;
    } else {
      gi_buf_append(&out, raw + pos, seg_total);
    }

    if (m == GI_JPEG_SOS) {
      pos += seg_total;
      gi_buf_append(&out, raw + pos, file_size - pos);
      break;
    }
    pos += seg_total;
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
 * Lossy path (libjpeg-turbo)
 * ════════════════════════════════════════════════════════════════════════════
 */

/**
 * @brief Lossy re-encode via libjpeg-turbo with web-optimized settings.
 *
 * Decodes to RGB, re-encodes with progressive + 4:2:2 chroma subsampling.
 * All metadata excluded by construction.
 *
 * @param src     Source file path.
 * @param dst     Destination file path.
 * @param quality Re-encode quality 1-100.
 * @param dry_run If 1, compute result without writing.
 * @param result  Output struct.
 */
static void jpeg_lossy(const char *src, const char *dst, int quality,
                       int dry_run, GiCleanResult *result) {
  result->size_before = file_size_of(src);
  result->was_lossy = 1;
  result->quality = quality;
  result->segments_removed = 0;

  if (dry_run) {
    /* Skip decode entirely — estimate output size */
    result->size_after = result->size_before * 35 / 100;
    return;
  }

  /* ── Decode ── */
  struct jpeg_decompress_struct dec;
  struct jpeg_error_mgr jerr;

  dec.err = jpeg_std_error(&jerr);
  jpeg_create_decompress(&dec);

  FILE *fin = fopen(src, "rb");
  if (!fin) {
    jpeg_destroy_decompress(&dec);
    set_error(result, "cannot open file");
    return;
  }

  jpeg_stdio_src(&dec, fin);
  jpeg_read_header(&dec, TRUE);
  dec.out_color_space = JCS_RGB;
  jpeg_start_decompress(&dec);

  int width = (int)dec.output_width;
  int height = (int)dec.output_height;
  int components = dec.output_components;
  size_t row_stride = (size_t)(width * components);
  size_t img_bytes = row_stride * (size_t)height;

  unsigned char *pixels = (unsigned char *)malloc(img_bytes);
  if (!pixels) {
    jpeg_destroy_decompress(&dec);
    fclose(fin);
    set_error(result, "out of memory");
    return;
  }

  while (dec.output_scanline < dec.output_height) {
    unsigned char *row = pixels + dec.output_scanline * row_stride;
    jpeg_read_scanlines(&dec, &row, 1);
  }
  jpeg_finish_decompress(&dec);
  jpeg_destroy_decompress(&dec);
  fclose(fin);

  result->size_before = file_size_of(src);
  result->was_lossy = 1;
  result->quality = quality;

  /* ── Encode ── */
  struct jpeg_compress_struct enc;
  struct jpeg_error_mgr jerr_enc;

  enc.err = jpeg_std_error(&jerr_enc);
  jpeg_create_compress(&enc);

  unsigned char *out_buf = NULL;
  unsigned long out_size = 0;
  jpeg_mem_dest(&enc, &out_buf, &out_size);

  enc.image_width = (JDIMENSION)width;
  enc.image_height = (JDIMENSION)height;
  enc.input_components = components;
  enc.in_color_space = JCS_RGB;

  jpeg_set_defaults(&enc);
  jpeg_set_quality(&enc, quality, TRUE);

  /* 4:2:2 chroma — good balance between size and color fidelity */
  enc.comp_info[0].h_samp_factor = 2;
  enc.comp_info[0].v_samp_factor = 1;
  enc.comp_info[1].h_samp_factor = 1;
  enc.comp_info[1].v_samp_factor = 1;
  enc.comp_info[2].h_samp_factor = 1;
  enc.comp_info[2].v_samp_factor = 1;

  /* Progressive encoding — smaller files, better for web */
  jpeg_simple_progression(&enc);

  jpeg_start_compress(&enc, TRUE);
  while (enc.next_scanline < enc.image_height) {
    unsigned char *row = pixels + enc.next_scanline * row_stride;
    jpeg_write_scanlines(&enc, &row, 1);
  }
  jpeg_finish_compress(&enc);
  jpeg_destroy_compress(&enc);
  free(pixels);

  result->size_after = (size_t)out_size;
  result->segments_removed = 0;
  result->was_lossy = 1;
  result->quality = quality;

  GiBuffer wrapped;
  wrapped.data = out_buf;
  wrapped.size = (size_t)out_size;
  wrapped.cap = (size_t)out_size;

  if (gi_buf_write(dst, &wrapped) != 0)
    set_error(result, "failed to write output file");

  free(out_buf);
}

/* ════════════════════════════════════════════════════════════════════════════
 * Public API
 * ════════════════════════════════════════════════════════════════════════════
 */

int gi_jpeg_get_info(const char *path, GiImageInfo *info) {
  size_t file_size = 0;
  unsigned char *raw = load_file(path, &file_size);
  if (!raw)
    return -1;
  if (file_size < 2 || raw[0] != 0xFF || raw[1] != GI_JPEG_SOI) {
    free(raw);
    return -1;
  }

  memset(info, 0, sizeof(*info));
  info->type = GI_IMG_JPEG;
  info->file_size = file_size;

  int found_jfif = 0;
  int found_exif = 0;

  size_t pos = 2;
  while (pos + 3 < file_size) {
    if (raw[pos] != 0xFF)
      break;
    unsigned char m = raw[pos + 1];
    if (m == GI_JPEG_EOI)
      break;
    if (m == GI_JPEG_SOI) {
      pos += 2;
      continue;
    }
    if (m == 0xFF) {
      pos++;
      continue;
    }

    unsigned short slen = gi_read_be16(raw + pos + 2);
    if (slen < 2)
      break;

    unsigned char *sd = raw + pos + 4;
    size_t sb = (size_t)(slen - 2);

    if (m == GI_JPEG_APP0 && sb >= 4 && memcmp(sd, "JFIF", 4) == 0)
      found_jfif = 1;

    if (m == GI_JPEG_APP1 && parse_exif(sd, sb, info) == 0) {
      found_exif = 1;
      info->has_exif = 1;
    }

    /* SOF markers: C0-C3, C5-C7, C9-CB, CD-CF */
    if ((m >= 0xC0 && m <= 0xC3) || (m >= 0xC5 && m <= 0xC7) ||
        (m >= 0xC9 && m <= 0xCB) || (m >= 0xCD && m <= 0xCF)) {
      if (sb >= 5) {
        info->height = gi_read_be16(sd + 1);
        info->width = gi_read_be16(sd + 3);
      }
    }

    if (m == GI_JPEG_SOS)
      break;
    pos += 2 + (size_t)slen;
  }
  free(raw);

  snprintf(info->format, sizeof(info->format), "%s",
           found_jfif   ? "JPEG / JFIF"
           : found_exif ? "JPEG / EXIF"
                        : "JPEG");

  return 0;
}

void gi_jpeg_clean(const char *src, const char *dst, GiOptMode opt, int quality,
                   int dry_run, GiCleanResult *result) {
  memset(result, 0, sizeof(*result));
  result->success = 1;
  result->src = src;
  result->dst = dst;
  result->dry_run = dry_run;
  result->quality = quality;

  if (opt == GI_OPT_LOSSY)
    jpeg_lossy(src, dst, quality, dry_run, result);
  else
    jpeg_lossless(src, dst, dry_run, result);
}
