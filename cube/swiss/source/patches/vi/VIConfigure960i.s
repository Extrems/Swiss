#include "../asm.h"
#define _LANGUAGE_ASSEMBLY
#include "../../../../reservedarea.h"

.globl VIConfigure960i
VIConfigure960i:
	li			%r0, 0
	li			%r7, 1
	li			%r6, 0
	lhz			%r5, 8 (%r3)
	subfic		%r4, %r5, 240
	cmpwi		%r5, 240
	ble			3f
	li			%r0, 2
	cmpwi		%r5, 480
	ble			2f
	lhz			%r5, 6 (%r3)
	clrrwi		%r5, %r5, 1
	cmpwi		%r5, 480
	ble			1f
	li			%r5, 480
	sth			%r5, 6 (%r3)
1:	sth			%r5, 8 (%r3)
2:	subfic		%r4, %r5, 480
	srwi		%r4, %r4, 1
3:	sth			%r4, 12 (%r3)
	sth			%r5, 16 (%r3)
	stw			%r6, 20 (%r3)
	stb			%r7, 24 (%r3)
	stw			%r0, 0 (%r3)
	mflr		%r0
	trap

.globl VIConfigure960i_length
VIConfigure960i_length:
.long (VIConfigure960i_length - VIConfigure960i)