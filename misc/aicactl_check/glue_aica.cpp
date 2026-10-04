// 5.15 B1 test: nullAICA.cpp's globals, set up by the driver.
#include "plugs/nullAICA/nullAICA.h"
aica_setts aica_settings;
aica_init_params aica_params;
void ref_aica_params(u8* ram, u32* istext, HollyRaiseInterruptFP* raise, HollyCancelInterruptFP* cancel)
{
	aica_params.aica_ram = ram;
	aica_params.SB_ISTEXT = istext;
	aica_params.RaiseInterrupt = raise;
	aica_params.CancelInterrupt = cancel;
}
