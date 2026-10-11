# Plan section 7, bundle 8: what ps3_jit_mac.h and ps3_jit_bcache.h must emit for test_words() in
# jit8_check.cpp, in order. gen.py runs it through the build image's ppu-as into jit8_words.inc.
	.text
# mac lo9 hi10 a3 b4 l5 h6 x8
	extsw 8,3
	extsw 10,4
	mulld 8,8,10
	rldicl 9,5,0,32
	rldimi 9,6,32,0
	add 9,9,8
	rldicl 10,9,32,32
# mac lo0 hi11 a14 b28 l5 h6 x12
	extsw 12,14
	extsw 11,28
	mulld 12,12,11
	rldicl 0,5,0,32
	rldimi 0,6,32,0
	add 0,0,12
	rldicl 11,0,32,32
# bcache pc3 base 0x10B28000 i4 p5 v6
	rlwinm 4,3,1,15,28
	addis 5,0,0x10B3
	add 5,5,4
	lwz 6,-32768(5)
	cmpw 6,3
	bc 4,2,.+16
	lwz 6,-32764(5)
	mtctr 6
	bcctr 20,2
# bcache pc3 base 0x10B27FE0
	rlwinm 4,3,1,15,28
	addis 5,0,0x10B2
	add 5,5,4
	lwz 6,32736(5)
	cmpw 6,3
	bc 4,2,.+16
	lwz 6,32740(5)
	mtctr 6
	bcctr 20,2
# bcache pc31 base 0x00123400 i0 p5 v7
	rlwinm 0,31,1,15,28
	addis 5,0,0x12
	add 5,5,0
	lwz 7,13312(5)
	cmpw 7,31
	bc 4,2,.+16
	lwz 7,13316(5)
	mtctr 7
	bcctr 20,2
