/* BreadBox's private archive binds the legacy inline mprotect request here.
 * The unchanged PSX3D archive still binds the original SDK SVC. No core bytes
 * or data move: only this cold undefined symbol is rebound by objcopy. */
#include "app_policy.h"
#include <3ds.h>
#include <stddef.h>
#include "app_log.h"
#include "bb_arena.h"
Result bb_ps1_exec_request(Handle process,uintptr_t begin,uintptr_t alias,size_t bytes,u32 op,u32 perm)
{
 (void)process;
 bool ok=alias==0 && op==MEMOP_PROT && perm==7 && bb_exec_ready(begin,bytes);
 log_printf("arena: adapter exec request begin=%08lx bytes=%lu op=%lu perm=%lu ready=%d\n",
  (unsigned long)begin,(unsigned long)bytes,(unsigned long)op,(unsigned long)perm,ok);
 log_hold(false);
 return ok?0:(Result)0xd900060c;
}
