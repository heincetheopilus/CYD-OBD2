/**
 * @file media_service.h
 * Photo slideshow / frame-sequence animation player for the Media page.
 *
 * Hardware side of core/media_port.h (the UI talks to that header only). The
 * service owns the screen while playing (hal::display direct mode) and draws
 * JPEGs from the SD card with hal::jpeg. Every main loop iteration poll():
 *   1. evaluates the core::SafetyGate (vehicle speed, link, critical alerts),
 *   2. stops playback immediately when the gate closes (screen is handed back
 *      to LVGL and a forced-exit flag tells the page to return to the dashboard),
 *   3. draws at most one frame; the decode yields every few ms through the
 *      pump callback so the OBD link keeps being serviced, and is aborted as
 *      soon as the gate closes.
 * No delay() and no busy wait; no frame buffer.
 */
#pragma once

#include <stdint.h>

namespace hal::media {

/** Services that must keep running while a frame decodes (OBD link, alerts). */
using PumpFn = void (*)(uint32_t now_ms);

void init(PumpFn pump);

/** Call once per main loop iteration. */
void poll(uint32_t now_ms);

/**
 * Optional boot logo: mounts the card and draws /boot/logo.jpg (at most ~1.5 s).
 * On success the screen stays in direct mode (the LVGL splash is frozen) until
 * end_boot_logo() is called. Returns false if there is no card/file/valid JPEG;
 * the built-in splash is then shown as usual.
 */
bool show_boot_logo();
void end_boot_logo();

}  // namespace hal::media
