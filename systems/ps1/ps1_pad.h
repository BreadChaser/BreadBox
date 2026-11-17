// Suppress the core's automatic analog enable for the DualShock in port 1.
//
// The core switches a digital DualShock to analog on its own once a game has
// used config mode and then reads it 16 times in digital mode, unless the
// player has toggled the mode. Games that probe the pad, find it digital and
// keep it that way (Tomb Raider) then get 0x73 replies they cannot read, and
// the controls stop responding. Toggling twice marks the pad as set by the
// player and leaves its mode as it was; games still pick analog themselves
// (command 0x44), and players with the Controller setting or the shortcut.
//
// The mark lives in the pad state, so it is set again whenever the core's pad
// starts over: at game start (cards_begin), after BIOS initialization
// (game_presets_core_log, following the real pad reset) and after a state load
// (__wrap_retro_unserialize).
#ifndef PS1_PAD_H
#define PS1_PAD_H

int padToggleAnalog(unsigned int index);
int padAnalogMode(unsigned int index);

static inline void ps1_pad_settle(void)
{
	padToggleAnalog(0);
	padToggleAnalog(0);
}

#endif
