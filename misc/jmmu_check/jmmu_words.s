# Plan section 7, bundle 20 (P38): what ps3/ps3_jit_mmu.h must emit for jmmu_check.cpp's fixed cases (gen.py).
	.text
start:
# emit_jtc_lookup(r20, 0x10234560, 1), the miss label marked at its end
	rlwinm 12,20,25,19,28
	addis 11,12,0x1023
	lwz 12,0x4560(11)
	rlwinm 0,20,0,0,21
	cmplw 0,12,0
	bne 0,1f
	lwz 11,0x4564(11)
	rlwinm 0,20,0,22,31
	xori 0,0,3
1:
# emit_jtc_lookup(r3, 0x1023C560, 2): a table whose low half is negative
	rlwinm 12,3,25,19,28
	addis 11,12,0x1024
	lwz 12,-0x3AA0(11)
	rlwinm 0,3,0,31,21
	cmplw 0,12,0
	bne 0,1f
	lwz 11,-0x3A9C(11)
	rlwinm 0,3,0,22,31
	xori 0,0,2
1:
# emit_jtc_lookup(r27, 0x10234560, 4) and (r3, 0x10234560, 8)
	rlwinm 12,27,25,19,28
	addis 11,12,0x1023
	lwz 12,0x4560(11)
	rlwinm 0,27,0,30,21
	cmplw 0,12,0
	bne 0,1f
	lwz 11,0x4564(11)
	rlwinm 0,27,0,22,31
1:
	rlwinm 12,3,25,19,28
	addis 11,12,0x1023
	lwz 12,0x4560(11)
	rlwinm 0,3,0,29,21
	cmplw 0,12,0
	bne 0,1f
	lwz 11,0x4564(11)
	rlwinm 0,3,0,22,31
1:
# the loads: 1 into r20, 2 into r21, 4 into r3, 4 into f15; the pairs into r3:r4 and f14:f15
	lbzx 20,11,0
	extsb 20,20
	lhax 21,11,0
	lwzx 3,11,0
	lfsx 15,11,0
	add 11,11,0
	lwz 4,4(11)
	lwz 3,0(11)
	add 11,11,0
	lfs 14,0(11)
	lfs 15,4(11)
# the stores: 1 from r20, 2 from r4, 4 from r26, 4 from f16; the pairs from r5:r6 and f14:f15
	stbx 20,11,0
	sthx 4,11,0
	stwx 26,11,0
	stfsx 16,11,0
	add 11,11,0
	stw 5,0(11)
	stw 6,4(11)
	add 11,11,0
	stfs 14,0(11)
	stfs 15,4(11)
# jm_rldicl, jm_rldicr, jm_std, jm_ld
	clrldi 3,3,32
	rldicl 4,3,0,32
	rldicl 3,3,32,32
	rldicl 7,9,63,1
	rldicr 4,5,32,31
	rldicr 8,10,1,62
	std 3,0x48(30)
	ld 4,0x50(30)
	std 31,-8(1)
# emit_jmmu_cold_read: sz 1, areg r20, d r21, no FPR, cycles at 0x9C; done and stub = start; bl . for the call
	ori 3,20,0
	clrldi 3,3,32
	stw 29,0x9C(30)
	bl .
	addis 12,0,0x1024
	lwz 0,-0x5544(12)
	cmpwi 0,0,0
	bne 0,1f
	extsb 21,3
	b start
1:
	addis 3,0,0x8C01
	ori 3,3,0x2345
	b start
# emit_jmmu_cold_read: sz 4, areg r3, FPR f17 at 0x48, no cycles
	clrldi 3,3,32
	bl .
	addis 12,0,0x1024
	lwz 0,-0x5544(12)
	cmpwi 0,0,0
	bne 0,1f
	stw 3,0x48(30)
	lfs 17,0x48(30)
	b start
1:
	addis 3,0,0x8C01
	ori 3,3,0x2345
	b start
# emit_jmmu_cold_read_pair, then with f14:f15
	clrldi 3,3,32
	bl .
	addis 12,0,0x1024
	lwz 0,-0x5544(12)
	cmpwi 0,0,0
	bne 0,1f
	rldicl 4,3,0,32
	rldicl 3,3,32,32
	b start
1:
	addis 3,0,0x8C01
	ori 3,3,0x2345
	b start
	clrldi 3,3,32
	bl .
	addis 12,0,0x1024
	lwz 0,-0x5544(12)
	cmpwi 0,0,0
	bne 0,1f
	std 3,0x48(30)
	lfs 14,0x48(30)
	lfs 15,0x4C(30)
	b start
1:
	addis 3,0,0x8C01
	ori 3,3,0x2345
	b start
# emit_jmmu_cold_write: sz 2 from r22 (areg r27), sz 1 from r4 (areg r3), sz 4 from f16
	ori 3,27,0
	clrldi 3,3,32
	ori 4,22,0
	andi. 4,4,0xFFFF
	bl .
	addis 12,0,0x1024
	lwz 0,-0x5544(12)
	cmpwi 0,0,0
	bne 0,1f
	b start
1:
	addis 3,0,0x8C01
	ori 3,3,0x2345
	b start
	clrldi 3,3,32
	andi. 4,4,0xFF
	bl .
	addis 12,0,0x1024
	lwz 0,-0x5544(12)
	cmpwi 0,0,0
	bne 0,1f
	b start
1:
	addis 3,0,0x8C01
	ori 3,3,0x2345
	b start
	clrldi 3,3,32
	stfs 16,0x48(30)
	lwz 4,0x48(30)
	bl .
	addis 12,0,0x1024
	lwz 0,-0x5544(12)
	cmpwi 0,0,0
	bne 0,1f
	b start
1:
	addis 3,0,0x8C01
	ori 3,3,0x2345
	b start
# emit_jmmu_cold_write_pair, then with f14:f15
	clrldi 3,3,32
	rldicr 4,5,32,31
	clrldi 6,6,32
	or 4,4,6
	bl .
	addis 12,0,0x1024
	lwz 0,-0x5544(12)
	cmpwi 0,0,0
	bne 0,1f
	b start
1:
	addis 3,0,0x8C01
	ori 3,3,0x2345
	b start
	clrldi 3,3,32
	stfs 14,0x48(30)
	stfs 15,0x4C(30)
	ld 4,0x48(30)
	bl .
	addis 12,0,0x1024
	lwz 0,-0x5544(12)
	cmpwi 0,0,0
	bne 0,1f
	b start
1:
	addis 3,0,0x8C01
	ori 3,3,0x2345
	b start
# emit_jmmu_fd_test(r30, 0xF0) with the label marked, then emit_jmmu_fd_exit
	lwz 0,0xF0(30)
	andi. 0,0,0x8000
	bne 0,1f
1:
	li 0,0x800
	addis 12,0,0x1024
	stw 0,-0x5544(12)
	addis 3,0,0x8C01
	ori 3,3,0x2345
	b start
# emit_jmmu_dispatch(0x10298000, 0x102B8AB0, 0x102BCDE0, r30, 0x114, 0x11C), 2 ways: three lows that are negative
	rlwinm 11,3,20,12,31
	xor 11,11,3
	rlwinm 11,11,4,15,26
	addis 11,11,0x102A
	lwz 12,-0x8000(11)
	cmplw 0,12,3
	beq 0,2f
	lwz 12,-0x7FF0(11)
	cmplw 0,12,3
	bne 0,1f
	addi 11,11,16
2:
	lwz 12,0x114(30)
	lwz 0,0x11C(30)
	rlwinm 12,12,2,31,31
	rlwinm 10,0,15,28,29
	or 12,12,10
	andi. 10,0,3
	xori 10,10,1
	cntlzw 10,10
	rlwinm 10,10,31,27,27
	or 12,12,10
	addis 10,0,0x102C
	lwz 10,-0x3220(10)
	rlwinm 10,10,8,16,23
	or 12,12,10
	lwz 0,-0x7FFC(11)
	cmplw 0,0,12
	bne 0,1f
	lwz 12,-0x7FF8(11)
	addis 10,0,0x102C
	lwz 10,-0x7550(10)
	cmplw 0,12,10
	bne 0,1f
	lwz 12,-0x7FF4(11)
	mtctr 12
	bcctr 20,2
1:
# emit_jmmu_sq_store, sq 0x102C8040, MMUCR 0x102D9F10, done = start: user scalar r22 at r20
	rlwinm 0,20,6,24,31
	cmplwi 0,0,0x38
	bne 0,1f
	addis 11,0,0x102E
	lwz 0,-0x60F0(11)
	andi. 0,0,0x200
	bne 0,1f
	rlwinm 12,20,0,26,29
	addis 12,12,0x102D
	stw 22,-0x7FC0(12)
	b start
1:
# privileged pair f14:f15 at r3
	rlwinm 0,3,6,23,31
	cmplwi 0,0,0x38
	bne 0,1f
	rlwinm 12,3,0,26,28
	addis 12,12,0x102D
	stfs 14,-0x7FC0(12)
	stfs 15,-0x7FBC(12)
	b start
1:
# privileged scalar f16 at r3
	rlwinm 0,3,6,24,31
	cmplwi 0,0,0x38
	bne 0,1f
	rlwinm 12,3,0,26,29
	addis 12,12,0x102D
	stfs 16,-0x7FC0(12)
	b start
1:
# user pair r5:r6 at r3
	rlwinm 0,3,6,23,31
	cmplwi 0,0,0x38
	bne 0,1f
	addis 11,0,0x102E
	lwz 0,-0x60F0(11)
	andi. 0,0,0x200
	bne 0,1f
	rlwinm 12,3,0,26,28
	addis 12,12,0x102D
	stw 5,-0x7FC0(12)
	stw 6,-0x7FBC(12)
	b start
1:
# emit_jmmu_pref: areg r20 with the cycle store at 0x9C, then r3 without it; each label marked at its end
	rlwinm 0,20,6,26,31
	cmplwi 0,0,0x38
	bne 0,1f
	ori 3,20,0
	clrldi 3,3,32
	stw 29,0x9C(30)
	bl .
	cmpwi 0,3,0
	bne 0,1f
1:
	rlwinm 0,3,6,26,31
	cmplwi 0,0,0x38
	bne 0,1f
	clrldi 3,3,32
	bl .
	cmpwi 0,3,0
	bne 0,1f
1:
# emit_jmmu_raise: r3 = the SPC, b stub (start)
	addis 3,0,0x8C01
	ori 3,3,0x2345
	b start
