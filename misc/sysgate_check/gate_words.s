# Plan 5.36 (gen.py): the words gate_emit_check.cpp lays out: the GATE count, the RESUME entry and a cycle exit.
# Context at r30: elapsed at 304, due at 308, next_pc at 272. nops mark loop_no_update, the update call, rv.
nu:	nop
	nop
	lwz 11,304(30)
	addi 11,11,1
	stw 11,304(30)
	lwz 12,308(30)
	cmpw 0,11,12
	blt 0,nu
lu:	nop
lg:	addi 29,29,1792
	lwz 11,304(30)
	addi 11,11,1
	stw 11,304(30)
	lwz 12,308(30)
	cmpw 0,11,12
	bltlr 0
	stw 3,272(30)
	b lu
rv:	nop
	lis 3,0x8c012345@h
	ori 3,3,0x2345
	clrldi 3,3,32
	bl lg
	b rv
