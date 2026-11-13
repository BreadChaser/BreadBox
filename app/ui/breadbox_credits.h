#ifndef BREADBOX_CREDITS_H
#define BREADBOX_CREDITS_H
/* breadbox_credits.c: paged text in the shared dialog. Both return when closed; after HOME > Close
 * (aptMainLoop() false) the caller must stop at once. */
void bb_credits_run(void);        /* About / Credits: cores, BIOS, libraries, assets, hand model, data and licenses */
void bb_install_help_run(void);   /* how a friend installs BreadBox from the QR codes */
#endif
