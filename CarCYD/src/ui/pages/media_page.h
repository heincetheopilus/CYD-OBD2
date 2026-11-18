/**
 * @file media_page.h
 * Full screen photo slideshow / animation (the shell hides the rail and the
 * status bar while this page is active).
 *
 * While playing, the media service draws straight to the panel and LVGL is
 * frozen; this page only handles touch and shows a message when there is
 * nothing to play (blocked, no card, no files).
 *
 *   tap                  next photo / animation
 *   tap top-right "P/A"  switch slideshow <-> animation
 *   tap top-left "x"     back to the dashboard
 *   long press           back to the dashboard
 */
#pragma once

#include "page.h"

namespace ui {

class MediaPage final : public Page {
 public:
  void create(lv_obj_t* parent) override;
  const char* title() const override;
  void on_show() override;
  void on_hide() override;
  void update(uint32_t now_ms) override;

 private:
  static void click_cb(lv_event_t* e);
  static void long_press_cb(lv_event_t* e);
  static void close_cb(lv_event_t* e);
  static void ack_cb(void* user);

  void show_message(const char* glyph, const char* title, const char* text, bool close_button);

  lv_obj_t* panel_ = nullptr;
  lv_obj_t* icon_ = nullptr;
  lv_obj_t* title_ = nullptr;
  lv_obj_t* text_ = nullptr;
  lv_obj_t* close_ = nullptr;
  bool waiting_ack_ = false;    ///< first-use safety notice is open
  uint32_t blocked_since_ = 0;  ///< when the current "blocked" message appeared (0 = none)
};

}  // namespace ui
