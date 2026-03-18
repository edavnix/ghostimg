/**
 * @file gi_platform.h
 * @brief Cross-platform filesystem utilities for Ghostimg.
 *
 * This module abstracts OS-specific operations: image type detection
 * by magic bytes, directory creation, output path construction, and
 * recursive image collection. Supports Linux, macOS, and Windows.
 */

#ifndef GI_PLATFORM_H
#define GI_PLATFORM_H

#include "gi_image.h"
#include <stddef.h>

/**
 * @brief Detects the image type of a file by reading its magic bytes.
 *
 * Does not rely on file extension — reads the first 12 bytes directly.
 *
 * @param path Path to the file.
 * @return Detected @ref GiImageType, or @c GI_IMG_UNKNOWN if unrecognized.
 */
GiImageType gi_platform_detect(const char *path);

/**
 * @brief Checks whether a path points to a directory.
 *
 * @param path Path to check.
 * @return 1 if it is a directory, 0 otherwise.
 */
int gi_platform_is_dir(const char *path);

/**
 * @brief Creates a directory at the given path.
 *
 * Does not create intermediate parent directories.
 * Permissions on POSIX: 0755.
 *
 * @param dir Path of the directory to create.
 * @return 0 on success, -1 on failure.
 */
int gi_platform_mkdir(const char *dir);

/**
 * @brief Constructs the destination path for a file inside an output directory.
 *
 * Extracts the basename from @p src and joins it with @p dir.
 * Example: src="photos/a.jpg", dir="clean" → dst="clean/a.jpg"
 *
 * @param src  Source file path.
 * @param dir  Output directory path.
 * @param dst  Output buffer for the resulting path.
 * @param sz   Size of the output buffer.
 * @return 0 on success, -1 if the result would overflow @p dst.
 */
int gi_platform_build_dst(const char *src, const char *dir, char *dst,
                          size_t sz);

/**
 * @brief Recursively collects all image files under a directory.
 *
 * Walks the directory tree and collects files whose magic bytes match
 * a supported format (JPEG, PNG, WebP). Hidden files and directories
 * (names starting with '.') are skipped.
 *
 * The caller is responsible for freeing each string in @p out_files
 * and then freeing the array itself.
 *
 * @param dir       Root directory to search.
 * @param out_files Output pointer to a heap-allocated array of file paths.
 * @return Number of files found, or -1 on error.
 */
int gi_platform_collect_images(const char *dir, char ***out_files);

#endif /* GI_PLATFORM_H */
