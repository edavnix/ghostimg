/**
 * @file platform.c
 * @brief Cross-platform filesystem utilities for Ghostimg.
 */

#include "gi_platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#else
#include <dirent.h>
#endif

/* ── Magic byte signatures ───────────────────────────────────────────────── */

static const unsigned char MAGIC_JPEG[2] = {0xFF, 0xD8};
static const unsigned char MAGIC_PNG[8] = {0x89, 0x50, 0x4E, 0x47,
                                           0x0D, 0x0A, 0x1A, 0x0A};
static const unsigned char MAGIC_RIFF[4] = {'R', 'I', 'F', 'F'};
static const unsigned char MAGIC_WEBP[4] = {'W', 'E', 'B', 'P'};

/* ── Internal: read first N bytes ───────────────────────────────────────── */

/**
 * @brief Reads the first @p n bytes of a file into @p hdr.
 * @param path Path to the file.
 * @param hdr  Output buffer, must be at least @p n bytes.
 * @param n    Number of bytes to read.
 * @return 0 on success, -1 if the file cannot be opened or is too short.
 */
static int read_magic(const char *path, unsigned char *hdr, size_t n) {
  FILE *f = fopen(path, "rb");
  if (!f)
    return -1;
  size_t r = fread(hdr, 1, n, f);
  fclose(f);
  return (r == n) ? 0 : -1;
}

/* ── Internal: dynamic file list ─────────────────────────────────────────── */

/**
 * @brief Growable list of heap-allocated file paths.
 */
typedef struct {
  char **files; /**< Array of heap-allocated path strings. */
  int count;    /**< Number of paths currently stored. */
  int cap;      /**< Allocated capacity of the array. */
} GiFileList;

/**
 * @brief Appends a copy of @p path to the file list, doubling on growth.
 * @param fl   List to append to.
 * @param path Path string to copy and store.
 * @return 0 on success, -1 on allocation failure.
 */
static int fl_push(GiFileList *fl, const char *path) {
  if (fl->count >= fl->cap) {
    int new_cap = fl->cap ? fl->cap * 2 : 64;
    char **tmp = (char **)realloc(fl->files, (size_t)new_cap * sizeof(char *));
    if (!tmp)
      return -1;
    fl->files = tmp;
    fl->cap = new_cap;
  }
  fl->files[fl->count] = (char *)malloc(strlen(path) + 1);
  if (!fl->files[fl->count])
    return -1;
  strcpy(fl->files[fl->count], path);
  fl->count++;
  return 0;
}

/* ── Internal: recursive collection ─────────────────────────────────────── */

#ifdef _WIN32

/**
 * @brief Recursively collects image files under @p dir on Windows.
 * @param dir Root directory to walk.
 * @param fl  File list to populate.
 */
static void collect_win(const char *dir, GiFileList *fl) {
  char pattern[4096];
  snprintf(pattern, sizeof(pattern), "%s\\*", dir);

  WIN32_FIND_DATAA fd;
  HANDLE h = FindFirstFileA(pattern, &fd);
  if (h == INVALID_HANDLE_VALUE)
    return;

  do {
    if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0)
      continue;

    char full[4096];
    snprintf(full, sizeof(full), "%s\\%s", dir, fd.cFileName);

    if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
      collect_win(full, fl);
    else if (gi_platform_detect(full) != GI_IMG_UNKNOWN)
      fl_push(fl, full);

  } while (FindNextFileA(h, &fd));

  FindClose(h);
}

#else

/**
 * @brief Recursively collects image files under @p dir on POSIX systems.
 *
 * Hidden entries (names starting with '.') are skipped.
 *
 * @param dir Root directory to walk.
 * @param fl  File list to populate.
 */
static void collect_posix(const char *dir, GiFileList *fl) {
  DIR *d = opendir(dir);
  if (!d)
    return;

  struct dirent *entry;
  while ((entry = readdir(d)) != NULL) {
    if (entry->d_name[0] == '.')
      continue;

    char full[4096];
    snprintf(full, sizeof(full), "%s/%s", dir, entry->d_name);

    struct stat st;
    if (stat(full, &st) != 0)
      continue;

    if (S_ISDIR(st.st_mode))
      collect_posix(full, fl);
    else if (S_ISREG(st.st_mode) && gi_platform_detect(full) != GI_IMG_UNKNOWN)
      fl_push(fl, full);
  }

  closedir(d);
}

#endif

/* ── Public API ──────────────────────────────────────────────────────────── */

GiImageType gi_platform_detect(const char *path) {
  unsigned char h[12] = {0};
  if (read_magic(path, h, sizeof(h)) != 0)
    return GI_IMG_UNKNOWN;

  if (memcmp(h, MAGIC_JPEG, 2) == 0)
    return GI_IMG_JPEG;
  if (memcmp(h, MAGIC_PNG, 8) == 0)
    return GI_IMG_PNG;
  if (memcmp(h, MAGIC_RIFF, 4) == 0 && memcmp(h + 8, MAGIC_WEBP, 4) == 0)
    return GI_IMG_WEBP;

  return GI_IMG_UNKNOWN;
}

int gi_platform_is_dir(const char *path) {
  struct stat st;
  if (stat(path, &st) != 0)
    return 0;
  return S_ISDIR(st.st_mode) ? 1 : 0;
}

int gi_platform_mkdir(const char *dir) {
#ifdef _WIN32
  return _mkdir(dir);
#else
  return mkdir(dir, 0755);
#endif
}

int gi_platform_build_dst(const char *src, const char *dir, char *dst,
                          size_t sz) {
  const char *base = strrchr(src, '/');
#ifdef _WIN32
  const char *base2 = strrchr(src, '\\');
  if (!base || (base2 && base2 > base))
    base = base2;
#endif
  base = base ? base + 1 : src;
  int r = snprintf(dst, sz, "%s/%s", dir, base);
  return (r > 0 && (size_t)r < sz) ? 0 : -1;
}

int gi_platform_collect_images(const char *dir, char ***out_files) {
  GiFileList fl = {NULL, 0, 0};
#ifdef _WIN32
  collect_win(dir, &fl);
#else
  collect_posix(dir, &fl);
#endif
  *out_files = fl.files;
  return fl.count;
}
