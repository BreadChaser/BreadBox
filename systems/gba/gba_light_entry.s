/* Preserve the SDK's original four-byte lighting entry slots. The journal's
   real implementations cannot fit there. A tail branch preserves the complete
   ARM calling convention, including lr, flags and arguments. */
.syntax unified
.arm
.section .text.C3Di_LightEnvUpdate,"ax",%progbits
.global C3Di_LightEnvUpdate
.type C3Di_LightEnvUpdate,%function
C3Di_LightEnvUpdate:
 b bb_gba_lightenv_update
.size C3Di_LightEnvUpdate, .-C3Di_LightEnvUpdate
.section .text.C3Di_LightEnvDirty,"ax",%progbits
.global C3Di_LightEnvDirty
.type C3Di_LightEnvDirty,%function
C3Di_LightEnvDirty:
 b bb_gba_lightenv_dirty
.size C3Di_LightEnvDirty, .-C3Di_LightEnvDirty
.section .note.GNU-stack,"",%progbits
