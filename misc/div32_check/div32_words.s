# Plan 5.38: what ps3_jit_div32.h must emit for test_words() in div32_check.cpp, in order. gen.py runs it
# through the build image's ppu-as into div32_words.inc.
	.text
# div32u q6 r7 a14 b15 h16 x8 y9
	rldicl 8,14,0,32
	rldimi 8,16,32,0
	rldicl 9,15,0,32
	divdu 6,8,9
	mulld 7,6,9
	subf 7,7,8
	neg 9,9
	sradi 9,9,63
	and 6,6,9
# div32u q6 r7 a3 b4 h5 x8 y9
	rldicl 8,3,0,32
	rldimi 8,5,32,0
	rldicl 9,4,0,32
	divdu 6,8,9
	mulld 7,6,9
	subf 7,7,8
	neg 9,9
	sradi 9,9,63
	and 6,6,9
# div32s q6 r7 a14 b15 h16 x8 y9 t10
	rldicl 8,14,0,32
	rldimi 8,16,32,0
	rldicl 10,8,1,63
	add 8,8,10
	extsw 9,15
	divd 6,8,9
	neg 10,9
	or 10,10,9
	sradi 10,10,63
	and 6,6,10
	mullw 7,6,9
	subf 7,7,8
	xor 10,16,15
	rlwinm 10,10,1,31,31
	subf 6,10,6
	rlwinm 9,16,1,31,31
	andc 9,9,10
	subf 7,9,7
# div32s q6 r7 a3 b4 h5 x8 y9 t10
	rldicl 8,3,0,32
	rldimi 8,5,32,0
	rldicl 10,8,1,63
	add 8,8,10
	extsw 9,4
	divd 6,8,9
	neg 10,9
	or 10,10,9
	sradi 10,10,63
	and 6,6,10
	mullw 7,6,9
	subf 7,7,8
	xor 10,5,4
	rlwinm 10,10,1,31,31
	subf 6,10,6
	rlwinm 9,5,1,31,31
	andc 9,9,10
	subf 7,9,7
# div32p2 d16 a16 b15 t5 s6 l7 x8 y9
	rlwinm 6,5,1,31,31
	xor 7,5,6
	rlwinm 7,7,0,31,31
	addi 7,7,-1
	addi 8,6,-1
	xor 9,15,8
	subf 9,8,9
	and 9,9,7
	extsw 8,15
	neg 8,8
	rldicl 8,8,1,63
	and 8,8,6
	subf 9,8,9
	add 16,16,9
# div32p2 d10 a3 b4 t5 s6 l7 x8 y9
	rlwinm 6,5,1,31,31
	xor 7,5,6
	rlwinm 7,7,0,31,31
	addi 7,7,-1
	addi 8,6,-1
	xor 9,4,8
	subf 9,8,9
	and 9,9,7
	extsw 8,4
	neg 8,8
	rldicl 8,8,1,63
	and 8,8,6
	subf 9,8,9
	add 10,3,9
# div32u_old q5 r6 a3 b4
	divwu 5,3,4
	mullw 6,5,4
	subf 6,6,3
# div32s_old q5 r6 a3 b4
	divw 5,3,4
	mullw 6,5,4
	subf 6,6,3
