// Host test for core::settings (defaults, sanitize of the v3 media fields, versioned persistence).
// Build: see tools/tests/run_tests.sh
#include <stdio.h>
#include <string.h>
#include <map>
#include <string>
#include <vector>

#include "../../CarCYD/src/core/persist.h"
#include "../../CarCYD/src/core/settings.h"

using namespace core;

static int g_fail = 0;
#define CHECK(c)                                           \
  do {                                                     \
    if (!(c)) {                                            \
      printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); \
      ++g_fail;                                            \
    }                                                      \
  } while (0)

class MemStorage : public Storage {
 public:
  std::map<std::string, std::vector<uint8_t>> blobs;
  bool read(const char* key, void* data, size_t len) override {
    auto it = blobs.find(key);
    if (it == blobs.end() || it->second.size() != len) return false;
    memcpy(data, it->second.data(), len);
    return true;
  }
  size_t size(const char* key) override {
    auto it = blobs.find(key);
    return it == blobs.end() ? 0 : it->second.size();
  }
  bool write(const char* key, const void* data, size_t len) override {
    const uint8_t* p = static_cast<const uint8_t*>(data);
    blobs[key].assign(p, p + len);
    return true;
  }
  void erase_all() override { blobs.clear(); }
};

int main() {
  {  // defaults: Media on, boot logo off
    const Settings d = settings::defaults();
    CHECK(d.media_enabled);
    CHECK(!d.boot_logo);
  }
  {  // change + flush + reload keeps the values (settings must survive a reboot)
    MemStorage st;
    CHECK(!settings::load(st));  // nothing stored yet
    Settings& s = settings::edit();
    s.brightness = 42;
    s.media_enabled = false;
    s.boot_logo = true;
    settings::commit(settings::kChangeMediaEnable);
    settings::flush();
    CHECK(st.size(storage_key::kSettings) > 0);
    s.brightness = 85;  // forget the live values, then load from "flash"
    s.media_enabled = true;
    s.boot_logo = false;
    CHECK(settings::load(st));
    CHECK(settings::get().brightness == 42);
    CHECK(!settings::get().media_enabled);
    CHECK(settings::get().boot_logo);
  }
  {  // a v2 blob (without the v3 tail) loads as a prefix; new fields keep their defaults
    MemStorage st;
    Settings old = settings::defaults();
    old.brightness = 55;
    const size_t v2_size = offsetof(Settings, media_enabled);
    persist::save(st, storage_key::kSettings, 2, &old, v2_size);
    CHECK(settings::load(st));
    CHECK(settings::get().brightness == 55);
    CHECK(settings::get().media_enabled);
    CHECK(!settings::get().boot_logo);
  }
  {  // flash corruption: bool bytes other than 0/1 become real bools
    Settings s = settings::defaults();
    uint8_t* raw = reinterpret_cast<uint8_t*>(&s);
    raw[offsetof(Settings, media_enabled)] = 7;
    raw[offsetof(Settings, boot_logo)] = 200;
    settings::edit() = s;
    settings::commit(0);
    const uint8_t a = reinterpret_cast<const uint8_t&>(settings::get().media_enabled);
    const uint8_t b = reinterpret_cast<const uint8_t&>(settings::get().boot_logo);
    CHECK(a == 1 && b == 1);
  }
  if (g_fail == 0) printf("settings: all tests passed\n");
  return g_fail == 0 ? 0 : 1;
}
