// Execute the real CPU reset and pad reset; replace unrelated memory, hardware
// and BIOS work. BIOS initialization reports completion before executing code.
#include <assert.h>
#include "r3000a.h"
#include "plugins.h"
#include "presets.h"

void psxMemReset(void) {}
void psxHwReset(void) {}
void psxBiosSetupBootState(void) {}
int BiosBootBypass(void) { return 0; }
static void cpu_reset(void) {}
static void cpu_apply(void) {}
static void bios_execute(psxRegisters *regs, enum blockExecCaller caller)
{
	assert(caller == EXEC_CALLER_BOOT);
	regs->pc = 0x80030000;
}
static R3000Acpu cpu = {
	.Reset = cpu_reset, .ApplyConfig = cpu_apply, .ExecuteBlock = bios_execute
};

void psxBiosInit(void)
{
	assert(!padAnalogMode(0));   // the real padReset cleared the old pad
#ifndef PS1_TEST_NO_RESET_NOTICE
	game_presets_core_log(Config.HLE ? "HLE BIOS initialized.\n" :
		"BIOS: 00000000, 'test BIOS', 'A'\n");
#endif
}

void core_reset(void)
{
	psxCpu = &cpu;
	psxReset();
}
