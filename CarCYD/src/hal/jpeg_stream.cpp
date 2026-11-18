#include "jpeg_stream.h"

#include <Arduino.h>
#include <SD.h>

#include "../core/jpeg_info.h"
#include "../core/log.h"
#include "lcd.h"

namespace hal::jpeg {
namespace {

size_t read_at(void* user, uint32_t offset, uint8_t* buf, size_t len) {
  File* f = static_cast<File*>(user);
  if (!f->seek(offset)) return 0;
  return f->read(buf, len);
}

/** LovyanGFX input stream over an SD file with cooperative yielding. */
class SdStream final : public lgfx::DataWrapper {
 public:
  SdStream(File& file, KeepGoingFn keep_going, void* user)
      : file_(file), keep_going_(keep_going), user_(user), last_yield_ms_(millis()) {
    need_transaction = false;  // SD is on its own bus, the panel transaction stays open
  }

  bool aborted() const { return aborted_; }

  int read(uint8_t* buf, uint32_t len) override {
    if (aborted_) return 0;
    const uint32_t now = millis();
    if (now - last_yield_ms_ >= kSliceMs) {
      if (keep_going_ != nullptr && !keep_going_(user_)) {
        aborted_ = true;
        return 0;  // makes the decoder fail immediately
      }
      last_yield_ms_ = millis();
    }
    return static_cast<int>(file_.read(buf, len));
  }
  void skip(int32_t offset) override { file_.seek(file_.position() + offset); }
  bool seek(uint32_t offset) override { return file_.seek(offset); }
  void close() override {}  // the caller owns the File
  int32_t tell() override { return static_cast<int32_t>(file_.position()); }

 private:
  File& file_;
  KeepGoingFn keep_going_;
  void* user_;
  uint32_t last_yield_ms_;
  bool aborted_ = false;
};

Result map_check(core::JpegCheck c) {
  switch (c) {
    case core::JpegCheck::Ok: return Result::Ok;
    case core::JpegCheck::Progressive: return Result::Progressive;
    case core::JpegCheck::Unsupported: return Result::Unsupported;
    case core::JpegCheck::TooLarge: return Result::TooLarge;
    case core::JpegCheck::NotJpeg:
    case core::JpegCheck::Truncated:
    default: return Result::NotJpeg;
  }
}

}  // namespace

Result draw_file(const char* path, int16_t area_w, int16_t area_h, KeepGoingFn keep_going, void* user) {
  File f = SD.open(path);
  if (!f || f.isDirectory()) return Result::OpenFailed;

  core::JpegInfo info;
  const Result checked = map_check(core::jpeg_check(read_at, &f, static_cast<uint32_t>(f.size()), info));
  if (checked != Result::Ok) {
    f.close();
    return checked;
  }
  const uint8_t div = core::jpeg_choose_div(info.width, info.height, static_cast<uint16_t>(area_w),
                                            static_cast<uint16_t>(area_h));
  if (div == 0) {
    f.close();
    return Result::TooLarge;
  }
  const int16_t out_w = static_cast<int16_t>((info.width + div - 1) / div);
  const int16_t out_h = static_cast<int16_t>((info.height + div - 1) / div);

  lgfx::LGFX_Device& dev = lcd();
  dev.waitDMA();
  if (out_w < area_w || out_h < area_h) dev.fillRect(0, 0, area_w, area_h, 0x0000);

  f.seek(0);
  SdStream stream(f, keep_going, user);
  const float scale = 1.0f / static_cast<float>(div);
  const bool ok = dev.drawJpg(&stream, (area_w - out_w) / 2, (area_h - out_h) / 2, out_w, out_h, 0, 0, scale, scale);
  f.close();

  if (stream.aborted()) return Result::Aborted;
  return ok ? Result::Ok : Result::DecodeError;
}

const char* result_name(Result r) {
  switch (r) {
    case Result::Ok: return "ok";
    case Result::OpenFailed: return "cannot open";
    case Result::NotJpeg: return "not a JPEG";
    case Result::Progressive: return "progressive JPEG";
    case Result::Unsupported: return "unsupported JPEG";
    case Result::TooLarge: return "too large";
    case Result::DecodeError: return "decode error";
    case Result::Aborted: return "aborted";
  }
  return "?";
}

}  // namespace hal::jpeg
