/**
 * @file jpeg_info.h
 * JPEG header inspection (pure, no Arduino): finds the frame size and rejects
 * files the on-device decoder cannot handle (progressive, arithmetic coding,
 * 12 bit) or that are too large, BEFORE any pixel is decoded.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace core {

constexpr uint32_t kJpegMaxFileBytes = 256u * 1024u;  ///< larger files are rejected
constexpr uint16_t kJpegMaxWidth = 1280;              ///< source size limits (decode time)
constexpr uint16_t kJpegMaxHeight = 960;

struct JpegInfo {
  uint16_t width = 0;
  uint16_t height = 0;
  uint8_t components = 0;
};

enum class JpegCheck : uint8_t {
  Ok = 0,
  NotJpeg,      ///< missing SOI / bad marker structure
  Truncated,    ///< ended before the frame header
  Progressive,  ///< SOF2
  Unsupported,  ///< arithmetic / lossless / 12 bit / odd component count
  TooLarge,     ///< file or image too big (or does not fit even at 1/8)
};

/** Reads `len` bytes at `offset`; returns the number of bytes read. */
using JpegReadFn = size_t (*)(void* user, uint32_t offset, uint8_t* buf, size_t len);

/** Parses the segments up to the frame header (SOF). */
JpegCheck jpeg_check(JpegReadFn read, void* user, uint32_t file_size, JpegInfo& out);

/**
 * Smallest decoder divisor (1, 2, 4, 8) that makes the image fit max_w x max_h.
 * Returns 0 if even 1/8 does not fit.
 */
uint8_t jpeg_choose_div(uint16_t width, uint16_t height, uint16_t max_w, uint16_t max_h);

}  // namespace core
