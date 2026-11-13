/* Optional instruction and scheduler counts.
 * BreadBox, GPL-2.0-or-later. Values are cumulative since content load. */
#ifndef GPSP_PROFILE_H
#define GPSP_PROFILE_H
#ifdef __cplusplus
extern "C" {
#endif
extern unsigned long long gpsp_profile[20];
void retro_profile_read(unsigned long long *out);
#ifdef __cplusplus
}
#endif
#ifdef GPSP_INSTRUCTION_PROFILE
#define PROFILE_INSTRUCTION() (++gpsp_profile[(reg[REG_PC] >> 24) & 15])
#define PROFILE_EVENT(n) (++gpsp_profile[n])
#else
#define PROFILE_INSTRUCTION() ((void)0)
#define PROFILE_EVENT(n) ((void)0)
#endif
#endif
