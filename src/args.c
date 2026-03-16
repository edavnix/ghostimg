/**
 * @file args.c
 * @brief Command-line argument parsing for Ghostimg.
 */

#include "gi_args.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** @brief Default re-encode quality when --lossy is given without a value. */
#define GI_DEFAULT_QUALITY 85

/* ── Internal: command parsing ───────────────────────────────────────────── */

/**
 * @brief Maps a command string to its @ref GiCommand value.
 *
 * @param str Command string from argv.
 * @return Matching @ref GiCommand, or @c GI_CMD_UNKNOWN if unrecognized.
 */
static GiCommand parse_command(const char *str) {
  if (strcmp(str, "info") == 0)
    return GI_CMD_INFO;
  if (strcmp(str, "clean") == 0)
    return GI_CMD_CLEAN;
  return GI_CMD_UNKNOWN;
}

/* ── Internal: option parsing ────────────────────────────────────────────── */

/**
 * @brief Parses --lossy and its quality argument.
 *
 * Advances @p i past the quality value on success.
 * If no value follows, defaults to @c GI_DEFAULT_QUALITY.
 *
 * @param argv Argument vector.
 * @param argc Total argument count.
 * @param i    Current index (pointing at --lossy). Updated on success.
 * @param args Output structure.
 * @return 0 on success, -1 if the quality value is out of range.
 */
static int parse_lossy(char *argv[], int argc, int *i, GiArgs *args) {
  args->opt_mode = GI_OPT_LOSSY;
  args->quality = GI_DEFAULT_QUALITY;

  /* Peek at next token — use it as quality if it looks like a number */
  if (*i + 1 < argc && argv[*i + 1][0] != '-') {
    int q = atoi(argv[*i + 1]);
    if (q < 1 || q > 100) {
      fprintf(stderr, "error  --lossy quality must be between 1 and 100.\n");
      return -1;
    }
    args->quality = q;
    (*i)++;
  }
  return 0;
}

/* ── Public API ──────────────────────────────────────────────────────────── */

int gi_args_parse(GiArgs *args, int argc, char *argv[]) {
  memset(args, 0, sizeof(*args));
  args->opt_mode = GI_OPT_LOSSLESS;
  args->quality = GI_DEFAULT_QUALITY;

  if (argc < 2)
    return -1;

  args->command = parse_command(argv[1]);
  if (args->command == GI_CMD_UNKNOWN)
    return -1;

  /* Pre-allocate file list — worst case all remaining argv are files */
  args->files = (char **)malloc((size_t)(argc - 2) * sizeof(char *));
  if (!args->files)
    return -1;

  for (int i = 2; i < argc; i++) {
    if (strcmp(argv[i], "--dry") == 0) {
      args->dry_run = 1;
    } else if (strcmp(argv[i], "--lossy") == 0) {
      if (parse_lossy(argv, argc, &i, args) != 0) {
        free(args->files);
        args->files = NULL;
        return -1;
      }
    } else {
      /* Treat anything that is not a known flag as a file/dir path */
      args->files[args->file_count++] = argv[i];
    }
  }

  return 0;
}

void gi_args_free(GiArgs *args) {
  free(args->files);
  args->files = NULL;
  args->file_count = 0;
}
