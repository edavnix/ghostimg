/**
 * @file gi_image.h
 * @brief Shared types, enumerations and result structs for all image parsers.
 *
 * This is the central contract between the processing layer (jpeg.c,
 * png.c, webp.c) and the presentation layer (ui.c, ui_report.c).
 * No parser should define its own result types — use these instead.
 */

#ifndef GI_IMAGE_H
#define GI_IMAGE_H

#include <stddef.h>

/**
 * @enum GiImageType
 * @brief Supported image formats, detected by magic bytes.
 */
typedef enum {
  GI_IMG_UNKNOWN = 0, /**< Unrecognized or unreadable format. */
  GI_IMG_JPEG,        /**< JPEG / JFIF  (FF D8 magic).        */
  GI_IMG_PNG,         /**< PNG          (89 50 4E 47 magic).  */
  GI_IMG_WEBP         /**< WebP         (RIFF....WEBP magic). */
} GiImageType;

/**
 * @enum GiStripMode
 * @brief Metadata removal strategy, shared by all parsers.
 */
typedef enum {
  GI_STRIP_ALL, /**< Remove all non-essential metadata. */
  GI_STRIP_GPS  /**< Remove only GPS location data.     */
} GiStripMode;

/**
 * @enum GiOptMode
 * @brief Processing strategy, shared by all parsers.
 */
typedef enum {
  GI_OPT_LOSSLESS, /**< Strip metadata only — pixel data untouched. */
  GI_OPT_LOSSY     /**< Decode and re-encode at target quality.     */
} GiOptMode;

/**
 * @struct GiImageInfo
 * @brief Metadata report for a single image file.
 *
 * Populated by gi_jpeg_get_info(), gi_png_get_info(), gi_webp_get_info().
 * Never printed inside parsers — passed to ui_report for display.
 */
typedef struct {
  GiImageType type;    /**< Detected format. */
  char format[64];     /**< Human-readable format string. */
  unsigned int width;  /**< Image width in pixels. */
  unsigned int height; /**< Image height in pixels. */
  size_t file_size;    /**< File size in bytes. */

  /* Metadata presence flags */
  int has_exif;     /**< 1 if EXIF data was found. */
  int has_gps;      /**< 1 if GPS coordinates were found. */
  int has_xmp;      /**< 1 if XMP metadata was found. */
  int has_icc;      /**< 1 if ICC color profile was found. */
  int has_comments; /**< 1 if text comments were found. */

  /* EXIF fields — populated when has_exif == 1 */
  char make[128];     /**< Camera manufacturer. */
  char model[128];    /**< Camera model. */
  char datetime[32];  /**< Original capture date/time. */
  char software[128]; /**< Processing software name. */

  /* GPS fields — populated when has_gps == 1 */
  double gps_lat;      /**< Latitude in decimal degrees. */
  double gps_lon;      /**< Longitude in decimal degrees. */
  char gps_lat_ref[4]; /**< 'N' or 'S'. */
  char gps_lon_ref[4]; /**< 'E' or 'W'. */
} GiImageInfo;

/**
 * @struct GiCleanResult
 * @brief Outcome of a single clean or dry-run operation.
 *
 * Populated by gi_jpeg_clean(), gi_png_clean(), gi_webp_clean().
 * Never printed inside parsers — passed to ui_report for display.
 */
typedef struct {
  const char *src;      /**< Source file path. */
  const char *dst;      /**< Destination file path. */
  int dry_run;          /**< 1 if no file was actually written. */
  int was_lossy;        /**< 1 if image data was re-encoded. */
  int quality;          /**< Re-encode quality used (lossy only). */
  size_t size_before;   /**< File size before cleaning, in bytes. */
  size_t size_after;    /**< File size after cleaning, in bytes. */
  int segments_removed; /**< Number of metadata segments/chunks dropped. */
  int success;          /**< 1 on success, 0 on error. */
  char error_msg[256];  /**< Error description if success == 0. */
} GiCleanResult;

#endif /* GI_IMAGE_H */
