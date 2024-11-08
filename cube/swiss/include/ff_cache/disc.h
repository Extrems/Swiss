/*
 disc.h
 Interface to the low level disc functions. Used by the higher level
 file system code.
 
 Copyright (c) 2006 Michael "Chishm" Chisholm
	
 Redistribution and use in source and binary forms, with or without modification,
 are permitted provided that the following conditions are met:

  1. Redistributions of source code must retain the above copyright notice,
     this list of conditions and the following disclaimer.
  2. Redistributions in binary form must reproduce the above copyright notice,
     this list of conditions and the following disclaimer in the documentation and/or
     other materials provided with the distribution.
  3. The name of the author may not be used to endorse or promote products derived
     from this software without specific prior written permission.

 THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR IMPLIED
 WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY
 AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR BE
 LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/
#ifndef _DISC_H
#define _DISC_H

#include "common.h"

#if 0
/** Disabled for Nintendont's FatFS cache. **/
/*
A list of all default devices to try at startup, 
terminated by a {NULL,NULL} entry.
*/
typedef struct {
	const char* name;
	DISC_INTERFACE* (*getInterface)(void);
} INTERFACE_ID;
extern const INTERFACE_ID _FAT_disc_interfaces[];
#endif

/*
Check if a disc is inserted
Return true if a disc is inserted and ready, false otherwise
*/
static inline bool _FAT_disc_isInserted (DISC_INTERFACE* disc) {
	return disc->isInserted(disc);
}

/*
Read numSectors sectors from a disc, starting at sector. 
numSectors is between 1 and LIMIT_SECTORS if LIMIT_SECTORS is defined,
else it is at least 1
sector is 0 or greater
buffer is a pointer to the memory to fill
*/
static inline bool _FAT_disc_readSectors (DISC_INTERFACE* disc, sec_t sector, sec_t numSectors, void* buffer) {
	// Nintendont: Attempt reading up to 10 times.
	int ret, i;
	for (i = 10; i > 0; i--) {
		ret = disc->readSectors (disc, sector, numSectors, buffer);
		if (ret != 0)
			break;
	}
	return ret;
}

/*
Write numSectors sectors to a disc, starting at sector. 
numSectors is between 1 and LIMIT_SECTORS if LIMIT_SECTORS is defined,
else it is at least 1
sector is 0 or greater
buffer is a pointer to the memory to read from
*/
static inline bool _FAT_disc_writeSectors (DISC_INTERFACE* disc, sec_t sector, sec_t numSectors, const void* buffer) {
	return disc->writeSectors (disc, sector, numSectors, buffer);
}

/*
Reset the card back to a ready state
*/
static inline bool _FAT_disc_clearStatus (DISC_INTERFACE* disc) {
	return disc->clearStatus(disc);
}

/*
Initialise the disc to a state ready for data reading or writing
*/
static inline bool _FAT_disc_startup (DISC_INTERFACE* disc) {
	return disc->startup(disc);
}

/*
Put the disc in a state ready for power down.
Complete any pending writes and disable the disc if necessary
*/
static inline bool _FAT_disc_shutdown (DISC_INTERFACE* disc) {
	return disc->shutdown(disc);
}

/*
Return a 32 bit value unique to each type of interface
*/
static inline uint32_t _FAT_disc_hostType (DISC_INTERFACE* disc) {
	return disc->ioType;
}

/*
Return a 32 bit value that specifies the capabilities of the disc
*/
static inline uint32_t _FAT_disc_features (DISC_INTERFACE* disc) {
	return disc->features;
}

#endif // _DISC_H
