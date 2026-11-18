#include "jpeg_info.h"

namespace core {
namespace {

constexpr int kMaxSegments = 64;

uint16_t be16(const uint8_t* p) { return static_cast<uint16_t>((p[0] << 8) | p[1]); }

}  // namespace

JpegCheck jpeg_check(JpegReadFn read, void* user, uint32_t file_size, JpegInfo& out) {
  if (read == nullptr) return JpegCheck::NotJpeg;
  if (file_size > kJpegMaxFileBytes) return JpegCheck::TooLarge;

  uint8_t b[8];
  if (read(user, 0, b, 2) != 2 || b[0] != 0xFF || b[1] != 0xD8) return JpegCheck::NotJpeg;

  uint32_t pos = 2;
  for (int seg = 0; seg < kMaxSegments; ++seg) {
    if (pos + 4 > file_size) return JpegCheck::Truncated;
    if (read(user, pos, b, 2) != 2) return JpegCheck::Truncated;
    if (b[0] != 0xFF) return JpegCheck::NotJpeg;

    // Skip fill bytes (0xFF 0xFF ...).
    uint8_t marker = b[1];
    uint32_t marker_pos = pos;
    while (marker == 0xFF) {
      if (++marker_pos + 2 > file_size) return JpegCheck::Truncated;
      if (read(user, marker_pos, b, 2) != 2) return JpegCheck::Truncated;
      if (b[0] != 0xFF) return JpegCheck::NotJpeg;
      marker = b[1];
    }
    pos = marker_pos + 2;

    if (marker == 0x01 || (marker >= 0xD0 && marker <= 0xD8)) continue;  // no payload
    if (marker == 0xD9 || marker == 0xDA) return JpegCheck::Truncated;    // EOI/SOS before SOF
    if (marker == 0x00) return JpegCheck::NotJpeg;

    if (read(user, pos, b, 2) != 2) return JpegCheck::Truncated;
    const uint16_t len = be16(b);
    if (len < 2) return JpegCheck::NotJpeg;

    const bool is_sof = marker >= 0xC0 && marker <= 0xCF && marker != 0xC4 && marker != 0xC8 && marker != 0xCC;
    if (!is_sof) {
      pos += len;
      continue;
    }

    if (marker == 0xC2) return JpegCheck::Progressive;
    if (marker != 0xC0) return JpegCheck::Unsupported;  // only baseline sequential
    if (len < 8 || read(user, pos + 2, b, 6) != 6) return JpegCheck::Truncated;

    const uint8_t precision = b[0];
    const uint16_t height = be16(b + 1);
    const uint16_t width = be16(b + 3);
    const uint8_t comps = b[5];
    if (precision != 8 || (comps != 1 && comps != 3)) return JpegCheck::Unsupported;
    if (width == 0 || height == 0) return JpegCheck::NotJpeg;
    if (width > kJpegMaxWidth || height > kJpegMaxHeight) return JpegCheck::TooLarge;

    out.width = width;
    out.height = height;
    out.components = comps;
    return JpegCheck::Ok;
  }
  return JpegCheck::NotJpeg;
}

uint8_t jpeg_choose_div(uint16_t width, uint16_t height, uint16_t max_w, uint16_t max_h) {
  for (uint8_t div = 1; div <= 8; div = static_cast<uint8_t>(div * 2)) {
    const uint32_t w = (static_cast<uint32_t>(width) + div - 1) / div;
    const uint32_t h = (static_cast<uint32_t>(height) + div - 1) / div;
    if (w <= max_w && h <= max_h) return div;
  }
  return 0;
}

}  // namespace core
