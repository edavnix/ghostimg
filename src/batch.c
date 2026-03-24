/**
 * @file batch.c
 * @brief Batch processing orchestration for Ghostimg.
 */

#include "gi_batch.h"
#include "gi_jpeg.h"
#include "gi_platform.h"
#include "gi_png.h"
#include "gi_ui.h"
#include "gi_ui_report.h"
#include "gi_webp.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── Internal: file list ─────────────────────────────────────────────────── */

/**
 * @brief Ownership flag for a file path entry.
 *
 * Paths collected from directories are heap-allocated and must be freed.
 * Paths that come directly from argv point into the caller's memory.
 */
typedef struct {
  char *path; /**< File path string. */
  int owned;  /**< 1 if heap-allocated (must be freed), 0 if from argv. */
} GiFileEntry;

/**
 * @brief Expands all inputs into a flat list of @ref GiFileEntry.
 *
 * Directories are expanded recursively via
 * @ref gi_platform_collect_images. Regular file paths are added directly.
 *
 * @param args      Parsed CLI arguments.
 * @param out       Output array of entries.
 * @param out_count Number of entries written.
 * @return 0 on success, -1 on allocation failure.
 */
static int build_file_list(const GiArgs *args, GiFileEntry **out,
                           int *out_count) {
  int cap = args->file_count * 4;
  GiFileEntry *entries =
      (GiFileEntry *)malloc((size_t)cap * sizeof(GiFileEntry));
  if (!entries)
    return -1;

  int count = 0;

  for (int i = 0; i < args->file_count; i++) {
    const char *input = args->files[i];

    if (gi_platform_is_dir(input)) {
      char **found = NULL;
      int n = gi_platform_collect_images(input, &found);

      if (n <= 0) {
        char msg[512];
        snprintf(msg, sizeof(msg), "no images found in '%s'", input);
        gi_ui_warn(msg);
        if (found)
          free(found);
        continue;
      }

      gi_ui_info("directory scan complete");

      /* Grow if needed */
      if (count + n > cap) {
        int new_cap = (count + n) * 2;
        GiFileEntry *tmp = (GiFileEntry *)realloc(
            entries, (size_t)new_cap * sizeof(GiFileEntry));
        if (!tmp) {
          free(found);
          free(entries);
          return -1;
        }
        entries = tmp;
        cap = new_cap;
      }

      for (int j = 0; j < n; j++) {
        entries[count].path = found[j]; /* transfer ownership */
        entries[count].owned = 1;
        count++;
      }
      free(found); /* free the array, strings now owned by entries */

    } else {
      if (count >= cap) {
        int new_cap = cap * 2;
        GiFileEntry *tmp = (GiFileEntry *)realloc(
            entries, (size_t)new_cap * sizeof(GiFileEntry));
        if (!tmp) {
          free(entries);
          return -1;
        }
        entries = tmp;
        cap = new_cap;
      }
      entries[count].path = (char *)input; /* points into argv */
      entries[count].owned = 0;
      count++;
    }
  }

  *out = entries;
  *out_count = count;
  return 0;
}

/* ── Internal: single file dispatcher ───────────────────────────────────── */

/**
 * @brief Detects the image type and dispatches to the correct parser.
 *
 * For CMD_INFO: calls gi_*_get_info and passes result to
 *   @ref gi_ui_report_info.
 * For CMD_CLEAN: calls gi_*_clean and passes result to
 *   @ref gi_ui_report_clean.
 *
 * @param path  File path to process.
 * @param args  Parsed CLI arguments.
 * @return 0 on success, -1 on error or unsupported format.
 */
static int dispatch(const char *path, const GiArgs *args) {
  GiImageType type = gi_platform_detect(path);

  if (type == GI_IMG_UNKNOWN) {
    char msg[512];
    snprintf(msg, sizeof(msg), "'%s' is not a supported image (JPEG/PNG/WebP)",
             path);
    gi_ui_error(msg);
    return -1;
  }

  if (args->command == GI_CMD_INFO) {
    GiImageInfo info;
    int r = -1;

    if (type == GI_IMG_JPEG)
      r = gi_jpeg_get_info(path, &info);
    if (type == GI_IMG_PNG)
      r = gi_png_get_info(path, &info);
    if (type == GI_IMG_WEBP)
      r = gi_webp_get_info(path, &info);

    if (r != 0) {
      char msg[512];
      snprintf(msg, sizeof(msg), "cannot read '%s'", path);
      gi_ui_error(msg);
      return -1;
    }

    snprintf(info.path, sizeof(info.path), "%s", path);
    gi_ui_report_info(&info);
    return 0;
  }

  /* CMD_CLEAN */
  GiCleanResult result;

  if (type == GI_IMG_JPEG)
    gi_jpeg_clean(path, path, args->opt_mode, args->quality, args->dry_run,
                  &result);
  else if (type == GI_IMG_PNG)
    gi_png_clean(path, path, args->opt_mode, args->dry_run, &result);
  else
    gi_webp_clean(path, path, args->opt_mode, args->quality, args->dry_run,
                  &result);

  gi_ui_report_clean(&result);
  return result.success ? 0 : -1;
}

/* ════════════════════════════════════════════════════════════════════════════
 * Public API
 * ════════════════════════════════════════════════════════════════════════════
 */

int gi_batch_run(const GiArgs *args) {
  if (args->file_count == 0) {
    gi_ui_error("no input files specified");
    return 1;
  }

  /* Expand inputs into flat file list */
  GiFileEntry *entries = NULL;
  int count = 0;

  if (build_file_list(args, &entries, &count) != 0) {
    gi_ui_error("out of memory");
    return 1;
  }

  if (count == 0) {
    gi_ui_error("no valid images to process");
    free(entries);
    return 1;
  }

  /* Process all files */
  int errors = 0;
  size_t bytes_before = 0;
  size_t bytes_after = 0;

  for (int i = 0; i < count; i++) {
    gi_ui_progress(entries[i].path, i + 1, count);

    /* Track sizes for summary (clean mode only) */
    if (args->command == GI_CMD_CLEAN) {
      GiCleanResult result;
      GiImageType type = gi_platform_detect(entries[i].path);

      if (type == GI_IMG_JPEG)
        gi_jpeg_clean(entries[i].path, entries[i].path, args->opt_mode,
                      args->quality, args->dry_run, &result);
      else if (type == GI_IMG_PNG)
        gi_png_clean(entries[i].path, entries[i].path, args->opt_mode,
                     args->dry_run, &result);
      else if (type == GI_IMG_WEBP)
        gi_webp_clean(entries[i].path, entries[i].path, args->opt_mode,
                      args->quality, args->dry_run, &result);
      else {
        char msg[512];
        snprintf(msg, sizeof(msg), "'%s' is not a supported image",
                 entries[i].path);
        gi_ui_error(msg);
        errors++;
        continue;
      }

      gi_ui_report_clean(&result);

      if (result.success) {
        bytes_before += result.size_before;
        bytes_after += result.size_after;
      } else {
        errors++;
      }

    } else {
      /* CMD_INFO — no size tracking needed */
      if (dispatch(entries[i].path, args) != 0)
        errors++;
    }
  }

  /* Summary — only for clean with multiple files */
  if (args->command == GI_CMD_CLEAN && count > 1)
    gi_ui_summary(count - errors, count, bytes_before, bytes_after);

  /* Free heap-allocated paths from directory expansion */
  for (int i = 0; i < count; i++)
    if (entries[i].owned)
      free(entries[i].path);
  free(entries);

  return errors > 0 ? 1 : 0;
}
