// Host test for core/jpeg_info.
#include <stdio.h>
#include <string.h>

#include <initializer_list>
#include <vector>

#include "../../CarCYD/src/core/jpeg_info.h"

using namespace core;
using Bytes = std::vector<uint8_t>;

static int g_fail = 0;
#define CHECK(c)                                           \
  do {                                                     \
    if (!(c)) {                                            \
      printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); \
      ++g_fail;                                            \
    }                                                      \
  } while (0)

static size_t rd(void* user, uint32_t off, uint8_t* buf, size_t len) {
  const Bytes& d = *static_cast<Bytes*>(user);
  if (off >= d.size()) return 0;
  size_t n = d.size() - off < len ? d.size() - off : len;
  memcpy(buf, d.data() + off, n);
  return n;
}

static void put(Bytes& v, std::initializer_list<int> l) {
  for (int x : l) v.push_back(static_cast<uint8_t>(x));
}

// SOI [APP1 of app_len bytes] SOF(marker) SOS
static Bytes make(int sof, int w, int h, int comps = 3, int app_len = 0, int precision = 8) {
  Bytes v;
  put(v, {0xFF, 0xD8});
  if (app_len > 0) {
    put(v, {0xFF, 0xE1, (app_len + 2) >> 8, (app_len + 2) & 0xFF});
    v.insert(v.end(), app_len, 0);
  }
  put(v, {0xFF, sof, 0, 8 + 3 * comps, precision, h >> 8, h & 0xFF, w >> 8, w & 0xFF, comps});
  v.insert(v.end(), 3 * comps, 0);
  put(v, {0xFF, 0xDA});
  return v;
}

static JpegCheck chk(Bytes& v, JpegInfo& i) { return jpeg_check(rd, &v, static_cast<uint32_t>(v.size()), i); }

int main() {
  JpegInfo i;
  {
    Bytes v = make(0xC0, 320, 240);
    CHECK(chk(v, i) == JpegCheck::Ok);
    CHECK(i.width == 320 && i.height == 240 && i.components == 3);
  }
  {  // large EXIF block is skipped
    Bytes v = make(0xC0, 640, 480, 3, 20000);
    CHECK(chk(v, i) == JpegCheck::Ok && i.width == 640);
  }
  { Bytes v = make(0xC2, 320, 240); CHECK(chk(v, i) == JpegCheck::Progressive); }
  { Bytes v = make(0xC1, 320, 240); CHECK(chk(v, i) == JpegCheck::Unsupported); }
  { Bytes v = make(0xC9, 320, 240); CHECK(chk(v, i) == JpegCheck::Unsupported); }
  { Bytes v = make(0xC0, 320, 240, 4); CHECK(chk(v, i) == JpegCheck::Unsupported); }
  { Bytes v = make(0xC0, 320, 240, 3, 0, 12); CHECK(chk(v, i) == JpegCheck::Unsupported); }
  { Bytes v = make(0xC0, 4000, 3000); CHECK(chk(v, i) == JpegCheck::TooLarge); }
  { Bytes v = make(0xC0, 0, 240); CHECK(chk(v, i) == JpegCheck::NotJpeg); }
  { Bytes v = {0x89, 'P', 'N', 'G'}; CHECK(chk(v, i) == JpegCheck::NotJpeg); }
  { Bytes v = {0xFF, 0xD8, 0xFF}; CHECK(chk(v, i) == JpegCheck::Truncated); }
  {  // segment length points past the end
    Bytes v = {0xFF, 0xD8, 0xFF, 0xE1, 0xFF, 0xF0, 0, 0, 0, 0};
    CHECK(chk(v, i) == JpegCheck::Truncated);
  }
  {  // file too big
    Bytes v = make(0xC0, 320, 240);
    CHECK(jpeg_check(rd, &v, kJpegMaxFileBytes + 1, i) == JpegCheck::TooLarge);
  }
  {  // fill bytes before a marker
    Bytes v;
    put(v, {0xFF, 0xD8, 0xFF, 0xFF, 0xFF, 0xC0, 0, 17, 8, 0, 240, 1, 64, 3});
    v.insert(v.end(), 9, 0);
    CHECK(chk(v, i) == JpegCheck::Ok && i.width == 320);
  }
  CHECK(jpeg_check(nullptr, nullptr, 10, i) == JpegCheck::NotJpeg);

  CHECK(jpeg_choose_div(320, 240, 320, 240) == 1);
  CHECK(jpeg_choose_div(321, 240, 320, 240) == 2);
  CHECK(jpeg_choose_div(640, 480, 320, 240) == 2);
  CHECK(jpeg_choose_div(1280, 960, 320, 240) == 4);
  CHECK(jpeg_choose_div(2560, 1920, 320, 240) == 8);
  CHECK(jpeg_choose_div(2561, 1920, 320, 240) == 0);
  CHECK(jpeg_choose_div(100, 50, 320, 240) == 1);

  printf(g_fail == 0 ? "jpeg_info: all tests passed\n" : "jpeg_info: %d FAILED\n", g_fail);
  return g_fail == 0 ? 0 : 1;
}
