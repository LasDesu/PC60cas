#ifndef EMUL_H
#define EMUL_H

#include <stdlib.h>

#define mem_alloc(s) malloc(s)

#define EMU_LE16(x)	(x)

#define cpu_tstates_frame (312*224)
#define emu_frame_rate 50.0

#endif