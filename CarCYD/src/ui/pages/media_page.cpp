#include "media_page.h"

#include "../../core/i18n.h"
#include "../../core/media_port.h"
#include "../components/dialog.h"
#include "../theme.h"
#include "../ui.h"
#include "../widgets.h"

namespace ui {
namespace {

constexpr int32_t kCornerZone = 48;      // exit / mode tap areas (resistive panel: generous)
constexpr uint32_t kAutoLeaveMs = 2500;  // a "blocked" message is shown this long, then the dashboard

// First-use notice. TODO: move to i18n.h (English only for now; i18n.h is being edited by Coder A).
constexpr const char* kSafetyTitle = "Safety notice";
constexpr const char* kSafetyText =
    "Pictures are shown only while the vehicle is stopped and stop automatically when it moves. "
    "Never watch the screen while driving.";

}  // namespace

const char* MediaPage::title() const { return core::tr(core::Str::PAGE_MEDIA); }

void MediaPage::create(lv_obj_t* parent) {
  root_ = box(parent);
  lv_obj_set_size(root_, lv_pct(100), lv_pct(100));
  lv_obj_set_style_bg_color(root_, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(root_, LV_OPA_COVER, 0);
  lv_obj_set_clickable(root_, true);
  lv_obj_add_event_cb(root_, click_cb, LV_EVENT_CLICKED, nullptr);
  lv_obj_add_event_cb(root_, long_press_cb, LV_EVENT_LONG_PRESSED, nullptr);

  // Message panel (blocked / no card / no files); hidden while playing.
  panel_ = column(root_, 8);
  lv_obj_set_size(panel_, 280, LV_SIZE_CONTENT);
  lv_obj_center(panel_);
  lv_obj_set_flex_align(panel_, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  icon_ = icon(panel_, ICON_INFO, font::icon24(), color::text_dim());
  title_ = label(panel_, font::value(), color::text(), "");
  lv_obj_set_width(title_, lv_pct(100));
  lv_obj_set_style_text_align(title_, LV_TEXT_ALIGN_CENTER, 0);
  text_ = label(panel_, font::body(), color::text_dim(), "");
  lv_obj_set_width(text_, lv_pct(100));
  lv_obj_set_style_text_align(text_, LV_TEXT_ALIGN_CENTER, 0);
  close_ = button(panel_, ICON_CLOSE, core::tr(core::Str::CLOSE), ButtonStyle::Secondary, close_cb, nullptr);
}

void MediaPage::on_show() {
  blocked_since_ = 0;
  waiting_ack_ = !core::media::safety_acknowledged();
  if (waiting_ack_) {
    dialog::confirm(kSafetyTitle, kSafetyText, core::tr(core::Str::OK), false, ack_cb, this);
    return;
  }
  core::media::activate();  // the shell calls update() right after
}

void MediaPage::ack_cb(void* user) {
  auto* self = static_cast<MediaPage*>(user);
  core::media::acknowledge_safety();
  self->waiting_ack_ = false;
  core::media::activate();
}

void MediaPage::on_hide() {
  waiting_ack_ = false;
  core::media::deactivate();
}

void MediaPage::show_message(const char* glyph, const char* title, const char* text, bool close_button) {
  set_visible(panel_, true);
  set_text(icon_, glyph);
  set_text(title_, title);
  set_text(text_, text);
  set_visible(close_, close_button);
}

void MediaPage::update(uint32_t now_ms) {
  using core::GateReason;
  using core::media::State;

  if (waiting_ack_) {
    // Dismissed without accepting (tap outside): leave, nothing is shown.
    if (!dialog::is_open()) navigate(PageId::Dashboard);
    return;
  }

  if (core::media::take_forced_exit()) {
    navigate(PageId::Dashboard);
    return;
  }

  bool blocked = false;
  switch (core::media::state()) {
    case State::Playing:
      set_visible(panel_, false);
      break;
    case State::NoCard:
      show_message(ICON_INFO, core::tr(core::Str::MEDIA_NO_CARD), core::tr(core::Str::MEDIA_NO_CARD_TEXT), true);
      break;
    case State::NoMedia:
      show_message(ICON_INFO, core::tr(core::Str::MEDIA_NO_MEDIA), core::tr(core::Str::MEDIA_NO_MEDIA_TEXT), true);
      break;
    case State::Waiting:
    case State::Off:
    default:
      switch (core::media::reason()) {
        case GateReason::Moving:
          show_message(ICON_ALERT, core::tr(core::Str::MEDIA_BLOCKED_MOVING), "", false);
          blocked = true;
          break;
        case GateReason::SpeedStale:
        case GateReason::LinkDown:
          show_message(ICON_ALERT, core::tr(core::Str::MEDIA_BLOCKED_UNKNOWN), "", false);
          blocked = true;
          break;
        case GateReason::CriticalAlert:
          show_message(ICON_ALERT, core::tr(core::Str::MEDIA_BLOCKED_ALERT), "", false);
          blocked = true;
          break;
        case GateReason::Settling:
        case GateReason::Allowed:
        default:
          show_message(ICON_INFO, core::tr(core::Str::MEDIA_SETTLING), "", false);
          break;
      }
      break;
  }

  // A blocked message does not stay: back to the dashboard shortly.
  if (!blocked) {
    blocked_since_ = 0;
  } else if (blocked_since_ == 0) {
    blocked_since_ = now_ms != 0 ? now_ms : 1;
  } else if (now_ms - blocked_since_ >= kAutoLeaveMs) {
    navigate(PageId::Dashboard);
  }
}

void MediaPage::click_cb(lv_event_t* e) {
  if (core::media::state() != core::media::State::Playing) return;
  lv_point_t p{};
  lv_indev_get_point(lv_event_get_indev(e), &p);
  if (p.y < kCornerZone && p.x < kCornerZone) {
    navigate(PageId::Dashboard);
  } else if (p.y < kCornerZone && p.x >= layout::kScreenW - kCornerZone) {
    core::media::toggle_mode();
  } else {
    core::media::next();
  }
}

void MediaPage::long_press_cb(lv_event_t*) { navigate(PageId::Dashboard); }

void MediaPage::close_cb(lv_event_t*) { navigate(PageId::Dashboard); }

}  // namespace ui
