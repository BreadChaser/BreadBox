/* Verbatim from devkitPro/libctru v2.7.0, libctru/source/services/apt.c.
 * zlib license; Copyright (c) 2014-2026 libctru contributors.
 * Event thread only sets FLAG_SHOULDSLEEP; caller dispatches hooks. */
void aptHandleSleep(void)
{
	if (!(aptFlags & FLAG_SHOULDSLEEP))
		return;

	aptFlags = (aptFlags &~ FLAG_SHOULDSLEEP) | FLAG_SLEEPING;
	aptCallHook(APTHOOK_ONSLEEP);
	APT_ReplySleepNotificationComplete(envGetAptAppId());
	LightEvent_Wait(&aptSleepEvent);
	aptFlags &= ~FLAG_SLEEPING;

	if (aptIsActive())
		GSPGPU_SetLcdForceBlack(0);
	aptCallHook(APTHOOK_ONWAKEUP);
}

bool aptMainLoop(void)
{
	aptHandleSleep();
	aptHandleJumpToHome();
	return !aptShouldClose();
}
