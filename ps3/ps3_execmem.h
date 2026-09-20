#pragma once

// Executable-memory probe (Dynarec.MD 3.1). Gated by nullDC.cfg [ps3] execprobe:
// 0 off (default: the question is settled), 1 run/resume, 2 reset the log then rerun.
void ps3_execmem_probe();
