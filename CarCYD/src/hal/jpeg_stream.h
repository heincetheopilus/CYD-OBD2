/**
 * @file jpeg_stream.h
 * Streams a baseline JPEG from the SD card straight to the panel, one MCU row
 * at a time - there is never a full frame buffer (no PSRAM on the CYD).
 *
 * Decoder: the TJpgDec port built into LovyanGFX (no extra library, shares the
 * panel driver; ~3.5 KB work area). The file is fed through a DataWrapper whose
 * read() is called every 512 B of compressed data; it hands control back to the
 * caller every `kSliceMs` (KeepGoingFn) so the OBD link keeps being serviced,
 * and the decode is aborted the moment the callback returns false.
 *
 * The header is inspected first (core/jpeg_info): progressive or oversized files
 * are rejected without decoding. Images are scaled by 1/2, 1/4 or 1/8 to fit.
 */
#pragma once

#include <stdint.h>

namespace hal::jpeg {

enum class Result : uint8_t {
  Ok = 0,
  OpenFailed,
  NotJpeg,
  Progressive,
  Unsupported,
  TooLarge,
  DecodeError,
  Aborted,  ///< KeepGoingFn returned false
};

/** Called about every kSliceMs while decoding; return false to abort. */
using KeepGoingFn = bool (*)(void* user);

constexpr uint32_t kSliceMs = 8;

/**
 * Draws `path` centred in the area (0,0)-(area_w,area_h). Clears the area first
 * when the picture is smaller than the area. The panel must be idle (no LVGL
 * flush in flight) - see hal::display::set_direct_mode().
 */
Result draw_file(const char* path, int16_t area_w, int16_t area_h, KeepGoingFn keep_going, void* user);

const char* result_name(Result r);

}  // namespace hal::jpeg
