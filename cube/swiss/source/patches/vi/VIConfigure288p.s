#include "../asm.h"
#define _LANGUAGE_ASSEMBLY
#include "../../../../reservedarea.h"

.globl VIConfigure288p
VIConfigure288p:
	li			%r0, 5
	li			%r7, 0
	li			%r6, 0
	lhz			%r5, 8 (%r3)
	slwi		%r5, %r5, 1
	cmpwi		%r5, 574
	ble			1f
	li			%r6, 1
	lhz			%r5, 8 (%r3)
1:	subfic		%r4, %r5, 574
	srwi		%r4, %r4, 1
	sth			%r4, 12 (%r3)
	sth			%r5, 16 (%r3)
	stw			%r6, 20 (%r3)
	stb			%r7, 24 (%r3)
	stw			%r0, 0 (%r3)
	mfmsr		%r3
	rlwinm		%r4, %r3, 0, 17, 15
	mtmsr		%r4
	extrwi		%r3, %r3, 1, 16
	blr

.globl VIConfigure288p_length
VIConfigure288p_length:
.long (VIConfigure288p_length - VIConfigure288p)