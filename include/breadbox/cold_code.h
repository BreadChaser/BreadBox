// The cold-code section for saves, menus and messages (core-first.ld)
#pragma once

// Code that runs only for saves, menus and messages: linked after all other
// frontend code (core-first.ld), so it never moves the per-frame code.
#define PSX3D_COLD __attribute__((noinline, section(".psx3d_cold.text")))
