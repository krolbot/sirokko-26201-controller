#pragma once

// Starts the local ST7789 + EC11 panel task. It never controls load GPIOs directly.
bool panel_begin();
bool panel_ready();
