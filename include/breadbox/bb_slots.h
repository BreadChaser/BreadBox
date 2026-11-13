/* The twelve save-state slots the player sees (1 .. 12) and the files behind them. GPL-2.0-or-later.
 *
 * BreadBox shows slot numbers 1..12 everywhere (board, pause, toasts). Files never moved:
 *  - shared save service (<base>.stateN): slots 1..10 are .state0 .. .state9, as before. The next
 *    file indices 10, 11 and 12 are Continue, Undo and the quick state (their files exist on players'
 *    cards), so slots 11 and 12 are the new .state13 and .state14.
 *  - PS1 (<prefix>.stN, savestates.c): slots 1..10 are .st0 .. .st9; Continue is .sta and Undo lives
 *    in the backups, so slots 11 and 12 are the new .st10 and .st11.
 * The board's card index is slot - 1 (0..11); Continue is the card after them. */
#ifndef BB_SLOTS_H
#define BB_SLOTS_H
#define BB_SLOTS 12            /* slots the player sees */
#define BB_SLOT_FIRST_NEW 13   /* shared save service: file index of slot 11 (slot 12 is 14) */
#define BB_SLOT_LAST 14        /* the highest file index the save service writes */
/* card index 0..11 <-> shared save service file index; -1 when there is none */
static inline int bb_slot_file(int card) { return card < 0 || card >= BB_SLOTS ? -1 : card < 10 ? card : card + 3; }
static inline int bb_slot_card(int file) { return file >= 0 && file <= 9 ? file : file >= 13 && file <= 14 ? file - 3 : -1; }
/* the number the player sees for a card index */
static inline int bb_slot_shown(int card) { return card + 1; }
/* PS1: card index <-> the N of <prefix>.stN (no gap: Continue and Undo are not numbered files) */
static inline int bb_ps1_slot_file(int card) { return card >= 0 && card < BB_SLOTS ? card : -1; }
static inline int bb_ps1_slot_card(int file) { return file >= 0 && file < BB_SLOTS ? file : -1; }
#endif
