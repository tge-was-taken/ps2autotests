#include "irx.h"

#include "intrman.h"
#include "stdio.h"
#include "sysclib.h"
#include "thsemap.h"

// What the shared harness needs.  A module a test imports for itself belongs
// in that test's imports.lst, since a module may only appear in one table.

stdio_IMPORTS_start
I_printf
stdio_IMPORTS_end

sysclib_IMPORTS_start
I_look_ctype_table
I_memcmp
I_memcpy
I_memset
I_sprintf
I_strcat
I_strcpy
I_strlen
I_strncmp
I_strtok
sysclib_IMPORTS_end

intrman_IMPORTS_start
I_CpuSuspendIntr
I_CpuResumeIntr
I_RegisterIntrHandler
I_ReleaseIntrHandler
I_EnableIntr
I_DisableIntr
I_CpuDisableIntr
I_CpuEnableIntr
I_QueryIntrContext
intrman_IMPORTS_end

thsemap_IMPORTS_start
I_CreateSema
I_DeleteSema
I_SignalSema
I_iSignalSema
I_WaitSema
I_PollSema
thsemap_IMPORTS_end
