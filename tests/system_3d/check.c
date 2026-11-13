#include <assert.h>
#define CFG_MODEL_3DS 0
#define CFG_MODEL_3DSXL 1
#define CFG_MODEL_N3DS 2
#define CFG_MODEL_2DS 3
#define CFG_MODEL_N3DSXL 4
#define CFG_MODEL_N2DSXL 5
#include "../../include/breadbox/system_3d.h"

int main(void)
{
	const unsigned models[] = {CFG_MODEL_3DS, CFG_MODEL_3DSXL, CFG_MODEL_N3DS,
		CFG_MODEL_2DS, CFG_MODEL_N3DSXL, CFG_MODEL_N2DSXL};
	for (unsigned i = 0; i < sizeof(models) / sizeof(models[0]); i++) {
		unsigned model = models[i];
		bool has_screen = system_3d_screen_model(model, CFG_MODEL_2DS, CFG_MODEL_N2DSXL);
		bool new_model = model == CFG_MODEL_N3DS || model == CFG_MODEL_N3DSXL || model == CFG_MODEL_N2DSXL;
		assert(system_ps1_3d_available(has_screen, new_model) == (new_model && model != CFG_MODEL_N2DSXL));
		assert(system_gba_3d_available(has_screen, new_model, true) == has_screen);
		assert(system_gba_3d_available(has_screen, new_model, false) == has_screen);
	}
	return 0;
}
