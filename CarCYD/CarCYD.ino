/**
 * CarCYD - Vehicle dashboard for the ESP32-2432S028R "Cheap Yellow Display"
 *
 * Board:     ESP32 Dev Module (esp32 by Espressif Systems, core 3.x)
 * Libraries: lvgl 9.x, LovyanGFX 1.2.x
 * Config:    lv_conf.h (this folder), src/config/board_config.h, src/config/app_config.h
 *
 * The sketch file only hands over to the application; see README.md for the
 * architecture and build instructions.
 */
#include <Arduino.h>

#include "src/app/app.h"
#include "src/config/app_config.h"

// The default 8 KB loop-task stack overflows while LVGL renders complex pages (measured: 64 B free).
SET_LOOP_TASK_STACK_SIZE(APP_LOOP_STACK_BYTES);

void setup() { app::setup(); }

void loop() { app::loop(); }
