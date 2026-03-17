/**
 * @file io.c
 * @brief Dynamic buffer management and atomic file I/O for Ghostimg.
 */

#include "gi_io.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#endif

/* ── Internal helpers ────────────────────────────────────────────────────── */

/**
 * @brief Grows the buffer capacity to at least @p needed bytes.
 *
 * Uses a doubling strategy to amortize reallocation cost.
 *
 * @param buf    Buffer to grow.
 * @param needed Minimum required capacity after growth.
 * @return 0 on success, -1 on allocation failure.
 */
static int gi_buf_grow(GiBuffer *buf, size_t needed) {
  size_t new_cap = buf->cap ? buf->cap * 2 : 4096;
  while (new_cap < needed)
    new_cap *= 2;

  unsigned char *ptr = (unsigned char *)realloc(buf->data, new_cap);
  if (!ptr)
    return -1;

  buf->data = ptr;
  buf->cap = new_cap;
  return 0;
}

/* ── Public API ──────────────────────────────────────────────────────────── */

int gi_buf_init(GiBuffer *buf, size_t initial_cap) {
  buf->data = (unsigned char *)malloc(initial_cap);
  if (!buf->data)
    return -1;
  buf->size = 0;
  buf->cap = initial_cap;
  return 0;
}

int gi_buf_append(GiBuffer *buf, const unsigned char *src, size_t n) {
  if (buf->size + n > buf->cap) {
    if (gi_buf_grow(buf, buf->size + n) != 0)
      return -1;
  }
  memcpy(buf->data + buf->size, src, n);
  buf->size += n;
  return 0;
}

int gi_buf_write(const char *path, const GiBuffer *buf) {
  char tmp[4096];
  if (snprintf(tmp, sizeof(tmp), "%s.gi_tmp", path) >= (int)sizeof(tmp))
    return -1;

  /* Write to temp file first */
  FILE *f = fopen(tmp, "wb");
  if (!f)
    return -1;

  if (fwrite(buf->data, 1, buf->size, f) != buf->size) {
    fclose(f);
    remove(tmp);
    return -1;
  }
  fclose(f);

  /* Atomic rename — no partial output visible on failure */
#ifdef _WIN32
  if (!MoveFileExA(tmp, path, MOVEFILE_REPLACE_EXISTING)) {
    remove(tmp);
    return -1;
  }
#else
  if (rename(tmp, path) != 0) {
    /* Cross-device fallback: copy then remove temp */
    FILE *in = fopen(tmp, "rb");
    FILE *out = fopen(path, "wb");
    if (in && out) {
      unsigned char block[65536];
      size_t n;
      while ((n = fread(block, 1, sizeof(block), in)) > 0)
        fwrite(block, 1, n, out);
    }
    if (in)
      fclose(in);
    if (out)
      fclose(out);
    remove(tmp);
  }
#endif

  return 0;
}

void gi_buf_free(GiBuffer *buf) {
  free(buf->data);
  buf->data = NULL;
  buf->size = 0;
  buf->cap = 0;
}
