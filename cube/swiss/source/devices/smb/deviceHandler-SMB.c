/**
 * Swiss - deviceHandler-SMB.c (originally from WiiSX)
 * Copyright (C) 2010-2014 emu_kidid
 * 
 * fileBrowser module for Samba based shares
 *
 * WiiSX homepage: https://code.google.com/p/swiss-gc/
 * email address:  emukidid@gmail.com
 *
 *
 * This program is free software; you can redistribute it and/
 * or modify it under the terms of the GNU General Public Li-
 * cence as published by the Free Software Foundation; either
 * version 2 of the Licence, or any later version.
 *
 * This program is distributed in the hope that it will be use-
 * ful, but WITHOUT ANY WARRANTY; without even the implied war-
 * ranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public Licence for more details.
 *
**/

#include <string.h>
#include <unistd.h>
#include <malloc.h>
#include <network.h>
#include <ogcsys.h>
#include <smb.h>
#include <sys/dir.h>
#include <sys/statvfs.h>
#include <fat.h>
#include "swiss.h"
#include "main.h"
#include "gui/FrameBufferMagic.h"
#include "gui/IPLFontWrite.h"
#include "deviceHandler.h"
#include "deviceHandler-FAT.h"
#include "deviceHandler-SMB.h"
#include "exi.h"
#include "patcher.h"

/* SMB Globals */
extern int net_initialized;
int smb_initialized = 0;

file_handle initial_SMB =
	{ "smb:/", // file name
	  0ULL,      // discoffset (u64)
	  0,         // offset
	  0,         // size
	  IS_DIR,
	  0,
	  0
	};

device_info initial_SMB_info = {
	0,
	0
};
	
device_info* deviceHandler_SMB_info() {
	return &initial_SMB_info;
}

void readDeviceInfoSMB() {
	struct statvfs buf;
	memset(&buf, 0, sizeof(statvfs));
	DrawFrameStart();
	DrawMessageBox(D_INFO,"Reading filesystem info for smb:/");
	DrawFrameFinish();
	
	int res = statvfs("smb:/", &buf);
	initial_SMB_info.freeSpaceInKB = !res ? (u32)((uint64_t)((uint64_t)buf.f_bsize*(uint64_t)buf.f_bfree)/1024LL):0;
	initial_SMB_info.totalSpaceInKB = !res ? (u32)((uint64_t)((uint64_t)buf.f_bsize*(uint64_t)buf.f_blocks)/1024LL):0;
}
	
// Connect to the share specified in settings.cfg
void init_samba() {
  
	int res = 0;

	if(smb_initialized) {
		return;
	}
	res = smbInit(&swissSettings.smbUser[0], &swissSettings.smbPassword[0], &swissSettings.smbShare[0], &swissSettings.smbServerIp[0]);
	print_gecko("SmbInit %i \r\n",res);
	if(res) {
		smb_initialized = 1;
	}
	else {
		smb_initialized = 0;
	}
}

s32 deviceHandler_SMB_readDir(file_handle* ffile, file_handle** dir, u32 type){	
   
	// We need at least a share name and ip addr in the settings filled out
	if(!strlen(&swissSettings.smbShare[0]) || !strlen(&swissSettings.smbServerIp[0])) {
		DrawFrameStart();
		sprintf(txtbuffer, "Check Samba Configuration");
		DrawMessageBox(D_FAIL,txtbuffer);
		DrawFrameFinish();
		wait_press_A();
		return SMB_SMBCFGERR;
	}

	if(!net_initialized) {       //Init if we have to
		DrawFrameStart();
		sprintf(txtbuffer, "Network has not been initialised");
		DrawMessageBox(D_FAIL,txtbuffer);
		DrawFrameFinish();
		wait_press_A();
		return SMB_NETINITERR;
	} 

	if(!smb_initialized) {       //Connect to the share
		init_samba();
		if(!smb_initialized) {
			DrawFrameStart();
			sprintf(txtbuffer, "Error initialising Samba");
			DrawMessageBox(D_FAIL,txtbuffer);
			DrawFrameFinish();
			wait_press_A();
			return SMB_SMBERR; //fail
		}
		readDeviceInfoSMB();
	}
		
	DIR* dp = opendir( ffile->name );
	if(!dp) return -1;
	struct dirent *entry;
	struct stat fstat;
	
	// Set everything up to read
	int num_entries = 1, i = 1;
	char file_name[1024];
	*dir = malloc( num_entries * sizeof(file_handle) );
	memset(*dir,0,sizeof(file_handle) * num_entries);
	(*dir)[0].fileAttrib = IS_SPECIAL;
	strcpy((*dir)[0].name, "..");
	
	// Read each entry of the directory
	while( (entry = readdir(dp)) != NULL ){
		if(strlen(entry->d_name) <= 2  && (entry->d_name[0] == '.' || entry->d_name[1] == '.')) {
			continue;
		}
		memset(&file_name[0],0,1024);
		sprintf(&file_name[0], "%s/%s", ffile->name, entry->d_name);
		stat(&file_name[0],&fstat);
		// Do we want this one?
		if((type == -1 || ((fstat.st_mode & S_IFDIR) ? (type==IS_DIR) : (type==IS_FILE)))) {
			if(!(fstat.st_mode & S_IFDIR)) {
				if(!checkExtension(entry->d_name)) continue;
			}
			// Make sure we have room for this one
			if(i == num_entries){
				++num_entries;
				*dir = realloc( *dir, num_entries * sizeof(file_handle) ); 
			}
			memset(&(*dir)[i], 0, sizeof(file_handle));
			sprintf((*dir)[i].name, "%s/%s", ffile->name, entry->d_name);
			(*dir)[i].offset = 0;
			(*dir)[i].size     = fstat.st_size;
			(*dir)[i].fileAttrib   = (fstat.st_mode & S_IFDIR) ? IS_DIR : IS_FILE;
			++i;
		}
	}
	
	closedir(dp);
	return num_entries;
}

s32 deviceHandler_SMB_seekFile(file_handle* file, u32 where, u32 type){
	if(type == DEVICE_HANDLER_SEEK_SET) file->offset = where;
	else if(type == DEVICE_HANDLER_SEEK_CUR) file->offset += where;
	return file->offset;
}

s32 deviceHandler_SMB_readFile(file_handle* file, void* buffer, u32 length){
	if(!file->fp) {
		file->fp = fopen( file->name, "rb" );
	}
	if(!file->fp) return -1;
	
	fseek(file->fp, file->offset, SEEK_SET);
	int bytes_read = fread(buffer, 1, length, file->fp);
	if(bytes_read > 0) file->offset += bytes_read;
	return bytes_read;
}

s32 deviceHandler_SMB_init(file_handle* file){
	return 1;
}

extern char *getDeviceMountPath(char *str);

s32 deviceHandler_SMB_deinit(file_handle* file) {
	if(smb_initialized) {
		smbClose("smb");
		smb_initialized = 0;
	}
	initial_SMB_info.freeSpaceInKB = 0;
	initial_SMB_info.totalSpaceInKB = 0;
	if(file && file->fp) {
		fclose(file->fp);
		file->fp = 0;
	}
	if(file) {
		char *mountPath = getDeviceMountPath(file->name);
		print_gecko("Unmounting [%s]\r\n", mountPath);
		fatUnmount(mountPath);
		free(mountPath);
	}
	return 1;
}

s32 deviceHandler_SMB_closeFile(file_handle* file) {
    return 0;
}

bool deviceHandler_SMB_test() {
	return exi_bba_exists();
}

s32 deviceHandler_SMB_setupFile(file_handle* file, file_handle* file2) {
	u32 *fragList = (u32*)VAR_FRAG_LIST;
	memset((void*)VAR_FRAG_LIST, 0, VAR_FRAG_SIZE);
	fragList[1] = file->size;
	*(volatile u32*)VAR_DISC_1_LBA = 0;
	*(volatile u32*)VAR_DISC_2_LBA = 0;
	*(volatile u32*)VAR_CUR_DISC_LBA = 0;
	*(volatile u8*)VAR_FILENAME_LEN = strlcpy((char*)VAR_FILENAME, strchr(file->name, '/') + 1, 235) + 1;
	net_get_mac_address((void*)VAR_CLIENT_MAC);
	*(volatile u32*)VAR_CLIENT_IP = net_gethostip();
	((volatile u8*)VAR_SERVER_MAC)[0] = 0xFF;
	((volatile u8*)VAR_SERVER_MAC)[1] = 0xFF;
	((volatile u8*)VAR_SERVER_MAC)[2] = 0xFF;
	((volatile u8*)VAR_SERVER_MAC)[3] = 0xFF;
	((volatile u8*)VAR_SERVER_MAC)[4] = 0xFF;
	((volatile u8*)VAR_SERVER_MAC)[5] = 0xFF;
	*(volatile u32*)VAR_SERVER_IP = inet_addr(swissSettings.smbServerIp);
	*(volatile u16*)VAR_IPV4_ID = 0;
	*(volatile u16*)VAR_FSP_KEY = 0;
	*(volatile u16*)VAR_FSP_DATA_LENGTH = 0;
	*(volatile u32*)VAR_FSP_POSITION = EOF;
	return 1;
}

DEVICEHANDLER_INTERFACE __device_smb = {
	DEVICE_ID_8,
	"Samba via BBA",
	"Must be pre-configured via swiss.ini",
	{TEX_SAMBA, 160, 85},
	FEAT_READ|FEAT_BOOT_GCM,
	LOC_SERIAL_PORT_1,
	&initial_SMB,
	(_fn_test)&deviceHandler_SMB_test,
	(_fn_info)&deviceHandler_SMB_info,
	(_fn_init)&deviceHandler_SMB_init,
	(_fn_readDir)&deviceHandler_SMB_readDir,
	(_fn_readFile)&deviceHandler_SMB_readFile,
	(_fn_writeFile)NULL,
	(_fn_deleteFile)NULL,
	(_fn_seekFile)&deviceHandler_SMB_seekFile,
	(_fn_setupFile)&deviceHandler_SMB_setupFile,
	(_fn_closeFile)&deviceHandler_SMB_closeFile,
	(_fn_deinit)&deviceHandler_SMB_deinit
};
