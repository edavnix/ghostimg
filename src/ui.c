/**
 * @file ui.c
 * @brief Terminal presentation layer for Ghostimg.
 */

#include "gi_ui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <io.h>
#define isatty _isatty
#define STDOUT_FILENO 1
#define STDERR_FILENO 2
#else
#include <unistd.h>
#endif

/** @brief 1 if color output is enabled (stdout is a TTY). */
static int gi_color = 0;

/* ANSI color codes */
#define COL_RESET "\033[0m"
#define COL_BOLD "\033[1m"
#define COL_RED "\033[31m"
#define COL_GREEN "\033[32m"
#define COL_YELLOW "\033[33m"
#define COL_CYAN "\033[36m"
#define COL_GRAY "\033[90m"
#define COL_WHITE "\033[97m"

/* Background + foreground label colors */
#define BG_RED "\033[41m\033[97m"
#define BG_GREEN "\033[42m\033[30m"
#define BG_YELLOW "\033[43m\033[30m"
#define BG_CYAN "\033[46m\033[30m"

/**
 * @brief Returns an ANSI code if color is enabled, empty string otherwise.
 * @param code ANSI escape sequence.
 * @return The code or "".
 */
static const char *c(const char *code) { return gi_color ? code : ""; }

/**
 * @brief Formats a byte count into a human-readable string.
 *
 * Output examples: "340 B", "1.2 KB", "3.4 MB".
 *
 * @param bytes  Byte count.
 * @param buf    Output buffer.
 * @param sz     Buffer size.
 */
static void fmt_bytes(size_t bytes, char *buf, size_t sz) {
  if (bytes < 1024)
    snprintf(buf, sz, "%zu B", bytes);
  else if (bytes < 1024 * 1024)
    snprintf(buf, sz, "%.1f KB", (double)bytes / 1024.0);
  else
    snprintf(buf, sz, "%.1f MB", (double)bytes / (1024.0 * 1024.0));
}

void gi_ui_init(void) { gi_color = isatty(STDOUT_FILENO); }

void gi_ui_info(const char *msg) {
  if (gi_color)
    printf("%s info %s  %s\n", BG_CYAN, COL_RESET, msg);
  else
    printf("info   %s\n", msg);
}

void gi_ui_success(const char *msg) {
  if (gi_color)
    printf("%s  ok  %s  %s\n", BG_GREEN, COL_RESET, msg);
  else
    printf("ok     %s\n", msg);
}

void gi_ui_warn(const char *msg) {
  if (gi_color)
    printf("%s warn %s  %s\n", BG_YELLOW, COL_RESET, msg);
  else
    printf("warn   %s\n", msg);
}

void gi_ui_error(const char *msg) {
  if (gi_color)
    fprintf(stderr, "%s error %s  %s\n", BG_RED, COL_RESET, msg);
  else
    fprintf(stderr, "error  %s\n", msg);
}

void gi_ui_progress(const char *path, int index, int total) {
  if (total > 1)
    printf("%s[%d/%d]%s  %s\n", c(COL_GRAY), index, total, c(COL_RESET), path);
  else
    printf("%s%s%s\n", c(COL_GRAY), path, c(COL_RESET));
}

void gi_ui_summary(int ok, int total, size_t bytes_before, size_t bytes_after) {
  char b_before[32], b_after[32], b_saved[32];
  fmt_bytes(bytes_before, b_before, sizeof(b_before));
  fmt_bytes(bytes_after, b_after, sizeof(b_after));

  size_t saved = bytes_before > bytes_after ? bytes_before - bytes_after : 0;
  fmt_bytes(saved, b_saved, sizeof(b_saved));

  double pct =
      bytes_before > 0 ? (double)saved / (double)bytes_before * 100.0 : 0.0;

  printf("\n");
  if (ok == total) {
    printf("%s done %s  %s%d/%d%s cleaned  "
           "%s → %s  %s-%s (%.0f%%)%s\n",
           c(BG_GREEN), c(COL_RESET), c(COL_BOLD), ok, total, c(COL_RESET),
           b_before, b_after, c(COL_GREEN), b_saved, pct, c(COL_RESET));
  } else {
    int failed = total - ok;
    printf("%s done %s  %s%d/%d%s cleaned  "
           "%s%d failed%s  "
           "%s → %s  %s-%s (%.0f%%)%s\n",
           c(BG_YELLOW), c(COL_RESET), c(COL_BOLD), ok, total, c(COL_RESET),
           c(COL_RED), failed, c(COL_RESET), b_before, b_after, c(COL_GREEN),
           b_saved, pct, c(COL_RESET));
  }
}

void gi_ui_help(const char *version) {
  printf("%sghostimg%s %s — image metadata remover and optimizer\n\n",
         c(COL_BOLD), c(COL_RESET), version);

  printf("%sUSAGE%s\n", c(COL_BOLD), c(COL_RESET));
  printf("  ghostimg <command> [options] <file|dir>\n\n");

  printf("%sCOMMANDS%s\n", c(COL_BOLD), c(COL_RESET));
  printf("  %sinfo%s   <path>             "
         "%sMetadata report — no files modified%s\n",
         c(COL_CYAN), c(COL_RESET), c(COL_GRAY), c(COL_RESET));
  printf("  %sclean%s  <path>             "
         "%sRemove metadata + compress, no quality loss%s\n",
         c(COL_CYAN), c(COL_RESET), c(COL_GRAY), c(COL_RESET));
  printf("  %sclean%s  <path> %s--dry%s      "
         "%sPreview what clean would do, no files written%s\n",
         c(COL_CYAN), c(COL_RESET), c(COL_YELLOW), c(COL_RESET), c(COL_GRAY),
         c(COL_RESET));
  printf("  %sclean%s  <path> %s--lossy%s %s<q>%s  "
         "%sClean + re-encode at exact quality 1-100%s\n",
         c(COL_CYAN), c(COL_RESET), c(COL_YELLOW), c(COL_RESET), c(COL_WHITE),
         c(COL_RESET), c(COL_GRAY), c(COL_RESET));

  printf("\n%sEXAMPLES%s\n", c(COL_BOLD), c(COL_RESET));
  printf("  ghostimg info   photo.jpg\n");
  printf("  ghostimg clean  photo.jpg\n");
  printf("  ghostimg clean  photo.jpg  --dry\n");
  printf("  ghostimg clean  photo.jpg  --lossy 75\n");
  printf("  ghostimg clean  ./photos/  --lossy 85\n");

  printf("\n%sOPTIONS%s\n", c(COL_BOLD), c(COL_RESET));
  printf("  %s--dry%s       Preview changes without writing any file\n",
         c(COL_YELLOW), c(COL_RESET));
  printf("  %s--lossy%s %s<q>%s  Re-encode at quality 1-100"
         " %s(implies compression)%s\n",
         c(COL_YELLOW), c(COL_RESET), c(COL_WHITE), c(COL_RESET), c(COL_GRAY),
         c(COL_RESET));
  printf("  %s--version%s   Print version and exit\n", c(COL_YELLOW),
         c(COL_RESET));
  printf("  %s--help%s      Print this help and exit\n\n", c(COL_YELLOW),
         c(COL_RESET));
}

void gi_ui_version(const char *version) { printf("ghostimg %s\n", version); }
