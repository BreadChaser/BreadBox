#ifndef PS1_MESSAGES_H
#define PS1_MESSAGES_H

/* Route core notices to the game screen or the diagnostic log. */
void ps1_core_message(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

#endif
