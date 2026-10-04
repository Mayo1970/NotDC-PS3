# Plan 5.18 (gen.sh): what emit_hot_cold_count must emit for a counter at 0x51B18000 (kind 0, lo < 0)
# and at 0x51B17FFC (kind 2, lo > 0).
	lis 4,0x51B2
	lwz 0,-32768(4)
	nor 0,0,0
	neg 0,0
	stw 0,-32768(4)
	lis 5,0x51B1
	lwz 0,0x7FFC(5)
	nor 0,0,0
	neg 0,0
	stw 0,0x7FFC(5)
