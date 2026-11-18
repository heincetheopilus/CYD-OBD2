#include "nav_rail.h"

#include "../theme.h"
#include "../widgets.h"

namespace ui {
namespace {

// Order = PageId. Media uses the grid glyph (no dedicated image icon generated yet).
const char* const kIcons[] = {ICON_DASHBOARD, ICON_LIVE, ICON_DIAG, ICON_TRIP, ICON_VIEW_GRID, ICON_SETTINGS};
static_assert(sizeof(kIcons) / sizeof(kIcons[0]) == static_cast<size_t>(PageId::Count), "one icon per page");
// The rail, ui.cpp and the hardware button rely on this PageId order (Media sits between Trip and Settings).
static_assert(static_cast<int>(PageId::Dashboard) == 0 && static_cast<int>(PageId::Trip) == 3 &&
                  static_cast<int>(PageId::Media) == 4 && static_cast<int>(PageId::Settings) == 5 &&
                  static_cast<int>(PageId::Count) == 6,
              "PageId order changed: update nav_rail icons and the Media hiding logic");

}  // namespace

void NavRail::create(lv_obj_t* parent, SelectFn on_select, bool media_enabled) {
  on_select_ = on_select;
  for (int i = 0; i < kItems; ++i) items_[i] = icons_[i] = markers_[i] = badges_[i] = nullptr;  // rebuilt shell

  lv_obj_t* rail = rail_ = box(parent);
  lv_obj_set_pos(rail, 0, 0);
  lv_obj_set_size(rail, layout::kRailW, layout::kScreenH);
  lv_obj_set_style_bg_color(rail, color::surface(), 0);
  lv_obj_set_style_bg_opa(rail, LV_OPA_COVER, 0);
  lv_obj_set_style_border_side(rail, LV_BORDER_SIDE_RIGHT, 0);
  lv_obj_set_style_border_width(rail, 1, 0);
  lv_obj_set_style_border_color(rail, color::stroke(), 0);

  int slot = 0;  // visual position (differs from the PageId once Media is hidden)
  for (int i = 0; i < kItems; ++i) {
    if (i == static_cast<int>(PageId::Media) && !media_enabled) continue;
    lv_obj_t* item = lv_button_create(rail);
    items_[i] = item;
    lv_obj_remove_style_all(item);
    lv_obj_set_pos(item, 0, slot++ * layout::kNavItemH);
    lv_obj_set_size(item, layout::kRailW - 1, layout::kNavItemH);
    lv_obj_set_style_bg_color(item, color::card_hi(), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(item, LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_set_user_data(item, this);
    lv_obj_add_event_cb(item, clicked_cb, LV_EVENT_CLICKED, reinterpret_cast<void*>(static_cast<intptr_t>(i)));
    add_touch_feedback(item);

    markers_[i] = box(item);
    lv_obj_set_size(markers_[i], 3, 24);
    lv_obj_align(markers_[i], LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_radius(markers_[i], 2, 0);
    lv_obj_set_style_bg_color(markers_[i], color::accent(), 0);
    lv_obj_set_style_bg_opa(markers_[i], LV_OPA_COVER, 0);

    icons_[i] = icon(item, kIcons[i], font::icon24(), color::text_dim());
    lv_obj_center(icons_[i]);

    badges_[i] = box(item);
    lv_obj_set_size(badges_[i], 8, 8);
    lv_obj_align(badges_[i], LV_ALIGN_CENTER, 12, -11);
    lv_obj_set_style_radius(badges_[i], LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(badges_[i], LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(badges_[i], 2, 0);
    lv_obj_set_style_border_color(badges_[i], color::surface(), 0);
    set_visible(badges_[i], false);
  }
}

void NavRail::set_active(PageId page) {
  for (int i = 0; i < kItems; ++i) {
    if (items_[i] == nullptr) continue;
    const bool active = i == static_cast<int>(page);
    set_visible(markers_[i], active);
    set_text_color(icons_[i], active ? color::accent() : color::text_dim());
    lv_obj_set_style_bg_color(items_[i], color::accent(), 0);
    lv_obj_set_style_bg_opa(items_[i], active ? LV_OPA_10 : LV_OPA_TRANSP, 0);
  }
}

void NavRail::set_shown(bool shown) { set_visible(rail_, shown); }

void NavRail::set_badge(PageId page, bool visible, lv_color_t c) {
  lv_obj_t* badge = badges_[static_cast<int>(page)];
  if (badge == nullptr) return;
  lv_obj_set_style_bg_color(badge, c, 0);
  set_visible(badge, visible);
}

void NavRail::clicked_cb(lv_event_t* e) {
  auto* self = static_cast<NavRail*>(lv_obj_get_user_data(lv_event_get_current_target_obj(e)));
  const int index = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
  if (self != nullptr && self->on_select_ != nullptr) self->on_select_(static_cast<PageId>(index));
}

}  // namespace ui
