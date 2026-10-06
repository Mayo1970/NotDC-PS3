# Plan 5.2: what ps3_jit_carry.h must emit for test_words() in carry_check.cpp, in order. gen.py runs it
# through the build image's ppu-as into carry_words.inc.
	.text
# addc d14 a14 b15 t5 x3 y4
	rldicl 3,14,0,32
	rldicl 4,15,0,32
	add 3,3,4
	add 14,3,5
	rldicl 5,14,32,63
# addc d6 a3 b4
	rldicl 3,3,0,32
	rldicl 4,4,0,32
	add 3,3,4
	add 6,3,5
	rldicl 5,6,32,63
# subc d14 a14 b15
	rldicl 3,14,0,32
	rldicl 4,15,0,32
	subf 3,4,3
	subf 14,5,3
	rldicl 5,14,32,63
# subc d6 a3 b28
	rldicl 3,3,0,32
	rldicl 4,28,0,32
	subf 3,4,3
	subf 6,5,3
	rldicl 5,6,32,63
# negc d14 a15 t5 x3
	rldicl 3,15,0,32
	add 3,3,5
	neg 14,3
	rldicl 5,14,32,63
# negc d6 a3
	rldicl 3,3,0,32
	add 3,3,5
	neg 6,3
	rldicl 5,6,32,63
# addv d14 a14 b15
	extsw 3,14
	extsw 4,15
	add 14,3,4
	extsw 5,14
	xor 5,5,14
	rldicl 5,5,1,63
# subv d6 a3 b28
	extsw 3,3
	extsw 4,28
	subf 6,4,3
	extsw 5,6
	xor 5,5,6
	rldicl 5,5,1,63
# div1 A14 B15 s3 t5 m4 q6 x8 y7
	rlwinm 4,3,23,31,31
	rlwinm 8,3,24,31,31
	xor 7,8,4
	rlwinm 6,14,1,31,31
	xor 6,6,8
	addi 8,7,-1
	xor 8,15,8
	rldicl 8,8,0,32
	xori 7,7,1
	rlwinm 14,14,1,0,30
	or 14,14,5
	add 14,14,8
	add 14,14,7
	rldicl 5,14,32,63
	xor 6,6,5
	xori 6,6,1
	xor 5,6,4
	xori 5,5,1
	rlwimi 3,6,8,23,23
# div1 A10 B9
	rlwinm 4,3,23,31,31
	rlwinm 8,3,24,31,31
	xor 7,8,4
	rlwinm 6,10,1,31,31
	xor 6,6,8
	addi 8,7,-1
	xor 8,9,8
	rldicl 8,8,0,32
	xori 7,7,1
	rlwinm 10,10,1,0,30
	or 10,10,5
	add 10,10,8
	add 10,10,7
	rldicl 5,10,32,63
	xor 6,6,5
	xori 6,6,1
	xor 5,6,4
	xori 5,5,1
	rlwimi 3,6,8,23,23
# cmpstr a14 b15 t5 c6 x0
	xor 5,14,15
	addis 6,0,0x0101
	ori 6,6,0x0101
	subf 0,6,5
	andc 0,0,5
	rlwinm 0,0,1,0,31
	and. 0,0,6
# cmpstr a3 b4
	xor 5,3,4
	addis 6,0,0x0101
	ori 6,6,0x0101
	subf 0,6,5
	andc 0,0,5
	rlwinm 0,0,1,0,31
	and. 0,0,6
# swaplb d4 s14
	rlwinm 4,14,0,0,15
	rlwimi 4,14,8,16,23
	rlwimi 4,14,24,24,31
# swaplb d4 s3
	rlwinm 4,3,0,0,15
	rlwimi 4,3,8,16,23
	rlwimi 4,3,24,24,31
