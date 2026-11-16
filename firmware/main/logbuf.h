/* Copyright (c) 2026 Michael Caldwell-Waller (chipguyhere). MIT License, see LICENSE in the repository root. */
#pragma once

/* (fork addition) ESP_LOGx through a ring buffer in the PSRAM, written to the UART by a low-priority task
 * (logbuf.c), so no task waits for the console. nothing without PSRAM */
void logbuf_start(void);
