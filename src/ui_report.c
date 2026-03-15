/**
 * @file ui_report.c
 * @brief Formatted report output for Ghostimg.
 */

#include "gi_ui.h"
#include "gi_ui_report.h"

#include <stdio.h>
#include <string.h>

#define COL_RESET "\033[0m"
#define COL_BOLD "\033[1m"
#define COL_CYAN "\033[36m"
#define COL_YELLOW "\033[33m"
#define COL_GREEN "\033[32m"
#define COL_RED "\033[31m"
#define COL_GRAY "\033[90m"
#define COL_WHITE "\033[97m"

#ifdef _WIN32
#include <io.h>
#define isatty _isatty
#define STDOUT_FILENO 1
#else
#include <unistd.h>
#endif

/**
 * @brief Returns an ANSI code if stdout is a TTY, empty string otherwise.
 * @param code ANSI escape sequence.
 * @return The code or "".
 */
static const char *c(const char *code) {
  return isatty(STDOUT_FILENO) ? code : "";
}

/**
 * @brief Formats a byte count into a human-readable string.
 * @param bytes Byte count.
 * @param buf   Output buffer.
 * @param sz    Buffer size.
 */
static void fmt_bytes(size_t bytes, char *buf, size_t sz) {
  if (bytes < 1024)
    snprintf(buf, sz, "%zu B", bytes);
  else if (bytes < 1024 * 1024)
    snprintf(buf, sz, "%.1f KB", (double)bytes / 1024.0);
  else
    snprintf(buf, sz, "%.2f MB", (double)bytes / (1024.0 * 1024.0));
}

/**
 * @brief Prints a labeled metadata field if the value is non-empty.
 *
 * @param label Field label (e.g. "Make").
 * @param value Field value string.
 */
static void print_field(const char *label, const char *value) {
  if (value && value[0])
    printf("  %s%-12s%s %s\n", c(COL_GRAY), label, c(COL_RESET), value);
}

/**
 * @brief Prints a section header for the report.
 * @param title Section title.
 */
static void print_section(const char *title) {
  printf("\n  %s%s%s\n", c(COL_BOLD), title, c(COL_RESET));
}

void gi_ui_report_info(const GiImageInfo *info) {
  char size_str[32];
  fmt_bytes(info->file_size, size_str, sizeof(size_str));

  /* ── Header ── */
  printf("\n%s  %s  %s", c(COL_BOLD), info->format, c(COL_RESET));
  printf("  %s%u × %u px%s", c(COL_GRAY), info->width, info->height,
         c(COL_RESET));
  printf("  %s%s%s\n", c(COL_CYAN), size_str, c(COL_RESET));

  /* ── File ── */
  print_section("File");
  print_field("Path", info->format); /* reuse slot — path set by caller */
  print_field("Format", info->format);

  /* ── EXIF ── */
  if (info->has_exif) {
    print_section("EXIF metadata");
    print_field("Make", info->make);
    print_field("Model", info->model);
    print_field("DateTime", info->datetime);
    print_field("Software", info->software);
  }

  /* ── GPS ── */
  if (info->has_gps) {
    print_section("GPS location");
    char lat_str[32], lon_str[32];
    snprintf(lat_str, sizeof(lat_str), "%.6f %s", info->gps_lat,
             info->gps_lat_ref);
    snprintf(lon_str, sizeof(lon_str), "%.6f %s", info->gps_lon,
             info->gps_lon_ref);
    print_field("Latitude", lat_str);
    print_field("Longitude", lon_str);
    printf("  %s!! GPS location data found — run clean to remove%s\n",
           c(COL_YELLOW), c(COL_RESET));
  }

  /* ── Other metadata ── */
  if (info->has_xmp || info->has_icc || info->has_comments) {
    print_section("Other");
    if (info->has_xmp)
      print_field("XMP", "present");
    if (info->has_icc)
      print_field("ICC", "present");
    if (info->has_comments)
      print_field("Comments", "present");
  }

  /* ── Clean status ── */
  printf("\n");
  if (!info->has_exif && !info->has_gps && !info->has_xmp &&
      !info->has_comments) {
    printf("  %salready clean — no metadata found%s\n\n", c(COL_GREEN),
           c(COL_RESET));
  } else {
    printf("  %srun: ghostimg clean <path> to remove all metadata%s\n\n",
           c(COL_GRAY), c(COL_RESET));
  }
}

void gi_ui_report_clean(const GiCleanResult *result) {
  if (!result->success) {
    gi_ui_error(result->error_msg);
    return;
  }

  char b_before[32], b_after[32], b_saved[32];
  fmt_bytes(result->size_before, b_before, sizeof(b_before));
  fmt_bytes(result->size_after, b_after, sizeof(b_after));

  size_t saved = result->size_before > result->size_after
                     ? result->size_before - result->size_after
                     : 0;
  fmt_bytes(saved, b_saved, sizeof(b_saved));

  double pct = result->size_before > 0
                   ? (double)saved / (double)result->size_before * 100.0
                   : 0.0;

  if (result->dry_run) {
    /* Preview line */
    printf("  %s~%s  %s  %s→%s  %s%s%s  "
           "%s-%s (%.0f%%)%s  %s%d segment(s) would be removed%s\n",
           c(COL_YELLOW), c(COL_RESET), b_before, c(COL_GRAY), c(COL_RESET),
           c(COL_CYAN), b_after, c(COL_RESET), c(COL_GREEN), b_saved, pct,
           c(COL_RESET), c(COL_GRAY), result->segments_removed, c(COL_RESET));
  } else {
    /* Result line */
    const char *mode = result->was_lossy ? "lossy" : "lossless";
    printf("  %s✓%s  %s  %s→%s  %s%s%s  "
           "%s-%s (%.0f%%)%s  %s%s q%d%s\n",
           c(COL_GREEN), c(COL_RESET), b_before, c(COL_GRAY), c(COL_RESET),
           c(COL_CYAN), b_after, c(COL_RESET), c(COL_GREEN), b_saved, pct,
           c(COL_RESET), c(COL_GRAY), mode, result->quality, c(COL_RESET));
  }
}
