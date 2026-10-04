# Plan 5.18 (gen.sh): one instruction per line, then the classes hot_classify must give it besides insn.
# Branch classes depend on the target, so hot_check.cpp builds those cases itself.
	lwz 3,8(30)          # ld ctx
	lwz 3,8(4)           # ld
	lwzu 3,8(4)          # ld
	stw 3,8(30)          # st ctx
	stfs 14,0x40(30)     # st ctx
	lfs 14,0x40(30)      # ld ctx
	stfd 14,0x40(30)     # st ctx
	lfd 14,0x40(30)      # ld ctx
	ld 3,8(30)           # ld ctx
	std 3,8(30)          # st ctx
	lha 3,0(4)           # ld mcode
	lhau 3,2(4)          # ld mcode
	lhax 3,4,5           # ld mcode
	lhaux 3,4,5          # ld mcode
	lwa 3,8(4)           # ld mcode
	lwax 3,4,5           # ld mcode
	lwaux 3,4,5          # ld mcode
	lha 3,8(30)          # ld ctx mcode
	lhz 3,0(4)           # ld
	lbz 3,0(4)           # ld
	lwzx 3,4,5           # ld
	lwzux 3,4,5          # ld
	lbzx 3,4,5           # ld
	lhzx 3,4,5           # ld
	ldx 3,4,5            # ld
	lwbrx 3,4,5          # ld
	lfsx 1,4,5           # ld
	lfdx 1,4,5           # ld
	lfd 1,8(4)           # ld
	ld 3,8(4)            # ld
	stwx 3,4,5           # st
	stwux 3,4,5          # st
	stbx 3,4,5           # st
	sthx 3,4,5           # st
	stdx 3,4,5           # st
	stfsx 1,4,5          # st
	stfdx 1,4,5          # st
	stfiwx 1,4,5         # st
	std 3,8(1)           # st
	stdu 1,-112(1)       # st
	stb 3,0(4)           # st
	sth 3,0(4)           # st
	stfd 1,8(4)          # st
	lmw 28,0(4)          # ld mcode
	stmw 28,0(4)         # st mcode
	mfcr 3               # mfcr
	mfocrf 3,0x80        #
	mtcrf 0xFF,3         # mcode
	mtcrf 0x80,3         #
	mtocrf 0x80,3        #
	slw 3,4,5            # vshift
	srw 3,4,5            # vshift
	sraw 3,4,5           # vshift
	sld 3,4,5            # vshift
	srd 3,4,5            # vshift
	srad 3,4,5           # vshift
	rlwnm 3,4,5,0,31     # vshift
	rldcl 3,4,5,0        # vshift
	rldcr 3,4,5,63       # vshift
	slwi 3,4,5           #
	srawi 3,4,5          #
	sradi 3,4,5          #
	rldicl 3,4,6,32      #
	mfxer 3              # xer
	mtxer 3              # xer
	mflr 3               #
	mtlr 3               #
	mtctr 3              #
	mfctr 3              #
	bctr                 # bctr
	bctrl                # callc
	blr                  #
	mulli 3,4,5          # muldiv
	mullw 3,4,5          # muldiv
	mullwo 3,4,5         # muldiv
	mulhw 3,4,5          # muldiv
	mulhwu 3,4,5         # muldiv
	divw 3,4,5           # muldiv
	divwu 3,4,5          # muldiv
	divwo 3,4,5          # muldiv
	mulld 3,4,5          # muldiv
	mulhd 3,4,5          # muldiv
	divd 3,4,5           # muldiv
	divdu 3,4,5          # muldiv
	fadds 1,2,3          # fpu
	fsubs 1,2,3          # fpu
	fmuls 1,2,3          # fpu
	fmadds 1,2,3,4       # fpu
	fnmsubs 1,2,3,4      # fpu
	fdivs 1,2,3          # fpu
	fsqrts 1,2           # fpu
	fres 1,2             # fpu
	fadd 1,2,3           # fpu
	fmul 1,2,3           # fpu
	fmadd 1,2,3,4        # fpu
	fsel 1,2,3,4         # fpu
	frsqrte 1,2          # fpu
	fcmpu 0,1,2          # fpu
	frsp 1,2             # fpu
	fctiwz 1,2           # fpu
	fcfid 1,2            # fpu
	fmr 1,2              # fmr
	fneg 1,2             # fmr
	fabs 1,2             # fmr
	fnabs 1,2            # fmr
	mffs 1               # fpscr
	mtfsf 0xFF,1         # fpscr
	mtfsfi 7,0           # fpscr
	mtfsb0 31            # fpscr
	mtfsb1 31            # fpscr
	mcrfs 0,7            # fpscr
	addi 3,4,5           #
	addis 3,4,5          #
	addic. 3,3,-5        #
	addic 3,3,-5         #
	ori 3,4,0            #
	or 3,4,4             #
	rlwinm 3,4,3,29,2    #
	xoris 0,0,0x6000     #
	cmplwi 0,0,7         #
	cmpwi 3,0            #
	and. 3,4,5           #
	andi. 0,4,0x1F       #
	extsb 3,4            #
	extsh 3,4            #
	clrldi 3,3,32        #
	nor 0,0,0            #
	neg 0,0              #
	add 3,4,5            #
	subf 3,4,5           #
	adde 3,4,5           #
	subfe 3,4,5          #
	addo 3,4,5           #
	cntlzw 3,4           #
	sync                 #
	isync                #
	nop                  #
	lis 4,0x51B2         #
