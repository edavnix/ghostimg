# Justfile for ghostimg
# Documentation: https://just.systems/man/en/

set shell := ["bash", "-euo", "pipefail", "-c"]

# Build config
BUILD_DIR := "build"
BINARY    := BUILD_DIR + "/ghostimg"

# Colors using ANSI escape codes
RED       := '\033[0;31m'
GREEN     := '\033[0;32m'
YELLOW    := '\033[0;33m'
CYAN      := '\033[0;36m'
GRAY      := '\033[0;90m'
END       := '\033[0m'

# Status Prefixes
ERROR     := RED    + "ERROR  " + END
INFO      := YELLOW + "INFO   " + END
SUCCESS   := GREEN  + "SUCCESS" + END
EXEC      := CYAN   + "EXEC   " + END

default: help

# Build ghostimg in debug mode (address + undefined sanitizers)
[no-exit-message]
build:
  #!/usr/bin/env bash
  echo -e "{{EXEC}} Executing Ghostimg"
  echo -e "{{INFO}} Building ghostimg (debug)..."
  cmake -B {{BUILD_DIR}} \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
  cmake --build {{BUILD_DIR}}
  echo -e "{{SUCCESS}} Binary ready: {{BINARY}}"

# Build ghostimg in release mode (optimized, stripped)
[no-exit-message]
release:
  #!/usr/bin/env bash
  echo -e "{{INFO}} Building ghostimg (release)..."
  cmake -B {{BUILD_DIR}} -DCMAKE_BUILD_TYPE=Release
  cmake --build {{BUILD_DIR}}
  echo -e "{{SUCCESS}} Binary ready: {{BINARY}}"

# Check binary exists or abort with a useful message
_require_binary:
  #!/usr/bin/env bash
  [[ -f "{{BINARY}}" ]] || {
    echo -e "{{ERROR}} Not built yet. Run: just build"
    exit 1
  }

# Show metadata report for a file or directory. Usage: just info <path>
[no-exit-message]
info path: _require_binary
  {{BINARY}} info "{{path}}"

# Remove all metadata (lossless). Usage: just clean <path>
[no-exit-message]
clean path: _require_binary
  {{BINARY}} clean "{{path}}"

# Remove metadata and re-encode at quality. Usage: just lossy <path> [quality]
[no-exit-message]
lossy path quality="85": _require_binary
  {{BINARY}} clean "{{path}}" --lossy --quality {{quality}}

# Preview what clean would remove without writing. Usage: just dry <path>
[no-exit-message]
dry path: _require_binary
  {{BINARY}} clean "{{path}}" --dry-run --verbose

# Delete the build directory
[no-exit-message]
wipe:
  #!/usr/bin/env bash
  rm -rf {{BUILD_DIR}}
  echo -e "{{SUCCESS}} Build directory removed"

# Show help
help:
  @echo -e "{{INFO}} ghostimg — available recipes:"
  @echo -e "  just build            {{CYAN}}# Debug build             {{END}}"
  @echo -e "  just release          {{CYAN}}# Release build           {{END}}"
  @echo -e "  just info <path>      {{CYAN}}# Metadata report         {{END}}"
  @echo -e "  just clean <path>     {{CYAN}}# Remove metadata         {{END}}"
  @echo -e "  just lossy <path> [q] {{CYAN}}# Re-encode at quality    {{END}}"
  @echo -e "  just dry <path>       {{CYAN}}# Preview without writing {{END}}"
  @echo -e "  just wipe             {{CYAN}}# Delete build directory  {{END}}"
