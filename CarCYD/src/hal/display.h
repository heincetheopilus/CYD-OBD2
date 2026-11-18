/**
 * @file display.h
 * LVGL display driver: partial rendering into two DMA-capable buffers that are
 * streamed to the panel while LVGL renders the next chunk.
 */
#pragma once

#include <lvgl.h>

namespace hal::display {

/** Initialises the panel and creates the LVGL display. Returns null on failure. */
lv_display_t* init(bool flipped);

/** Rotates the picture by 180 degrees (e.g. for an upside-down mounting). */
void set_flipped(bool flipped);

bool flipped();

/** Debug: time spent in flush_cb (incl. DMA wait) and number of flushes since the last call. */
void take_flush_stats(uint32_t& us, uint32_t& n);

/**
 * Direct mode: LVGL stops rendering (invalidation off, pending areas flushed
 * first) so a client may draw straight to the panel via lcd(). Leaving direct
 * mode repaints the whole UI. Used by the media service.
 */
void set_direct_mode(bool direct);
bool direct_mode();

}  // namespace hal::display
