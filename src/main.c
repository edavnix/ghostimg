/**
 * @file main.c
 * @brief Entry point for Ghostimg.
 *
 * Handles --version and --help before argument parsing, then
 * initializes the UI layer and delegates all processing to
 * gi_batch_run. No image logic lives here.
 */

#include "gi_args.h"
#include "gi_batch.h"
#include "gi_ui.h"

#include <string.h>

/** @brief Application version string. */
#define GI_VERSION "2.1.0"

/**
 * @brief Program entry point.
 *
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return 0 on success, 1 on error or invalid arguments.
 */
int main(int argc, char *argv[]) {
  gi_ui_init();

  if (argc < 2) {
    gi_ui_help(GI_VERSION);
    return 1;
  }

  if (strcmp(argv[1], "--version") == 0) {
    gi_ui_version(GI_VERSION);
    return 0;
  }

  if (strcmp(argv[1], "--help") == 0) {
    gi_ui_help(GI_VERSION);
    return 0;
  }

  GiArgs args;
  if (gi_args_parse(&args, argc, argv) != 0) {
    gi_ui_error("invalid command or arguments");
    gi_ui_help(GI_VERSION);
    return 1;
  }

  int r = gi_batch_run(&args);
  gi_args_free(&args);
  return r;
}
