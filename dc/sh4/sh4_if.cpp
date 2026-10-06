// sh4_if.cpp: the function-pointer layer between the emulator core and the SH4 backend (interpreter or dynarec).

#include "types.h"
#include "sh4_if.h"
#include "sh4_interpreter.h"

// Get_Sh4Interpreter() is in sh4_interpreter.cpp and Get_Sh4Recompiler() in rec_v2/driver.cpp, so each is
// defined once.

// Resources belong to Init/Term, so this only clears the function pointers.
void Release_Sh4If(sh4_if* cpu)
{
	if (cpu == nullptr)
		return;

	// Reset all function pointers to null for safety
	cpu->Run = nullptr;
	cpu->Stop = nullptr;
	cpu->Step = nullptr;
	cpu->Skip = nullptr;
	cpu->Reset = nullptr;
	cpu->Init = nullptr;
	cpu->Term = nullptr;
	cpu->ResetCache = nullptr;
	cpu->IsCpuRunning = nullptr;
}
