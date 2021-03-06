/*
	VitaShell
	Copyright (C) 2015-2016, TheFloW

	This program is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "main.h"
#include "io_process.h"
#include "package_installer.h"
#include "archive.h"
#include "file.h"
#include "message_dialog.h"
#include "language.h"
#include "utils.h"
#include "sha1.h"
#include "sysmodule_internal.h"
#include "libpromoter/promoterutil.h"

#include "resources/base_head_bin.h"

#define ntohl __builtin_bswap32

#define PACKAGE_PARENT "ux0:ptmp"
#define PACKAGE_DIR PACKAGE_PARENT "/pkg"
#define HEAD_BIN PACKAGE_DIR "/sce_sys/package/head.bin"

static char *title_id;

void loadScePaf() {
	uint32_t ptr[0x100] = { 0 };
	ptr[0] = 0;
	ptr[1] = (uint32_t)&ptr[0];
	uint32_t scepaf_argp[] = { 0x400000, 0xEA60, 0x40000, 0, 0 };
	sceSysmoduleLoadModuleInternalWithArg(0x80000008, sizeof(scepaf_argp), scepaf_argp, ptr);
}

int promote(char *path) {
	int res;

	loadScePaf();

	res = sceSysmoduleLoadModuleInternal(SCE_SYSMODULE_PROMOTER_UTIL);
	if (res < 0)
		return res;

	res = scePromoterUtilityInit();
	if (res < 0)
		return res;

	res = scePromoterUtilityPromotePkg(path, 0);
	if (res < 0)
		return res;

	int state = 0;
	do {
		res = scePromoterUtilityGetState(&state);
		if (res < 0)
			return res;

		sceKernelDelayThread(300 * 1000);
	} while (state);

	int result = 0;
	res = scePromoterUtilityGetResult(&result);
	if (res < 0)
		return res;

	res = scePromoterUtilityExit();
	if (res < 0)
		return res;

	res = sceSysmoduleUnloadModuleInternal(SCE_SYSMODULE_PROMOTER_UTIL);
	if (res < 0)
		return res;

	return result;
}

char *get_title_id(const char *filename) {
	char *res = NULL;
	long size = 0;
	FILE *fin = NULL;
	char *buf = NULL;
	int i;

	SfoHeader *header;
	SfoEntry *entry;
	
	fin = fopen(filename, "rb");
	if (!fin)
		goto cleanup;
	if (fseek(fin, 0, SEEK_END) != 0)
		goto cleanup;
	if ((size = ftell(fin)) == -1)
		goto cleanup;
	if (fseek(fin, 0, SEEK_SET) != 0)
		goto cleanup;
	buf = calloc(1, size + 1);
	if (!buf)
		goto cleanup;
	if (fread(buf, size, 1, fin) != 1)
		goto cleanup;

	header = (SfoHeader*)buf;
	entry = (SfoEntry*)(buf + sizeof(SfoHeader));
	for (i = 0; i < header->count; ++i, ++entry) {
		const char *name = buf + header->keyofs + entry->nameofs;
		const char *value = buf + header->valofs + entry->dataofs;
		if (name >= buf + size || value >= buf + size)
			break;
		if (strcmp(name, "TITLE_ID") == 0)
			res = strdup(value);
	}

cleanup:
	if (buf)
		free(buf);
	if (fin)
		fclose(fin);

	return res;
}

void fpkg_hmac(const uint8_t *data, unsigned int len, uint8_t hmac[16]) {
	SHA1_CTX ctx;
	uint8_t sha1[20];
	uint8_t buf[64];

	sha1_init(&ctx);
	sha1_update(&ctx, data, len);
	sha1_final(&ctx, sha1);

	memset(buf, 0, 64);
	memcpy(&buf[0], &sha1[4], 8);
	memcpy(&buf[8], &sha1[4], 8);
	memcpy(&buf[16], &sha1[12], 4);
	buf[20] = sha1[16];
	buf[21] = sha1[1];
	buf[22] = sha1[2];
	buf[23] = sha1[3];
	memcpy(&buf[24], &buf[16], 8);

	sha1_init(&ctx);
	sha1_update(&ctx, buf, 64);
	sha1_final(&ctx, sha1);
	memcpy(hmac, sha1, 16);
}

int makeHeadBin() {
	uint8_t hmac[16];
	uint32_t off;
	uint32_t len;
	uint32_t out;

	// head.bin must not be present
	SceIoStat stat;
	memset(&stat, 0, sizeof(SceIoStat));
	if (sceIoGetstat(HEAD_BIN, &stat) >= 0)
		return -1;

	// Get title id
	title_id = get_title_id(PACKAGE_DIR "/sce_sys/param.sfo");
	if (!title_id)// || strlen(title_id) != 9) // Enforce TITLEID format?
		return -2;

	// Allocate head.bin buffer
	uint8_t *head_bin = malloc(sizeof(base_head_bin));
	memcpy(head_bin, base_head_bin, sizeof(base_head_bin));

	// Write full titleid
	char full_title_id[128];
	snprintf(full_title_id, sizeof(full_title_id), "EP9000-%s_00-XXXXXXXXXXXXXXXX", title_id);
	strncpy((char *)&head_bin[0x30], full_title_id, 48);

	// hmac of pkg header
	len = ntohl(*(uint32_t *)&head_bin[0xD0]);
	fpkg_hmac(&head_bin[0], len, hmac);
	memcpy(&head_bin[len], hmac, 16);

	// hmac of pkg info
	off = ntohl(*(uint32_t *)&head_bin[0x8]);
	len = ntohl(*(uint32_t *)&head_bin[0x10]);
	out = ntohl(*(uint32_t *)&head_bin[0xD4]);
	fpkg_hmac(&head_bin[off], len - 64, hmac);
	memcpy(&head_bin[out], hmac, 16);

	// hmac of everything
	len = ntohl(*(uint32_t *)&head_bin[0xE8]);
	fpkg_hmac(&head_bin[0], len, hmac);
	memcpy(&head_bin[len], hmac, 16);

	// Make dir
	sceIoMkdir(PACKAGE_DIR "/sce_sys/package", 0777);

	// Write head.bin
	WriteFile(HEAD_BIN, head_bin, sizeof(base_head_bin));

	free(head_bin);


	return 0;
}

int install_thread(SceSize args_size, InstallArguments *args) {
	SceUID thid = -1;

	// Lock power timers
	powerLock();

	// Set progress to 0%
	sceMsgDialogProgressBarSetValue(SCE_MSG_DIALOG_PROGRESSBAR_TARGET_BAR_DEFAULT, 0);
	sceKernelDelayThread(DIALOG_WAIT); // Needed to see the percentage

	// Recursively clean up package_temp directory
	removePath(PACKAGE_PARENT, NULL, 0, NULL, NULL);
	sceIoMkdir(PACKAGE_PARENT, 0777);

	// Open archive
	int res = archiveOpen(args->file);
	if (res < 0) {
		closeWaitDialog();
		errorDialog(res);
		goto EXIT;
	}

	// Check permissions
	//我们这些玩盗版的不需要CheckPermission，^_^

	// Src path
	char src_path[MAX_PATH_LENGTH];
	strcpy(src_path, args->file);
	addEndSlash(src_path);

	// Get archive path info
	uint64_t size = 0;
	uint32_t folders = 0, files = 0;
	getArchivePathInfo(src_path, &size, &folders, &files);

	// Update thread
	thid = createStartUpdateThread(size + folders);

	// Extract process
	uint64_t value = 0;

	res = extractArchivePath(src_path, PACKAGE_DIR "/", &value, size + folders, SetProgress, cancelHandler);
	if (res <= 0) {
		closeWaitDialog();
		dialog_step = DIALOG_STEP_CANCELLED;
		errorDialog(res);
		goto EXIT;
	}

	// Make head.bin
	res = makeHeadBin();
	if (res < 0) {
		closeWaitDialog();
		errorDialog(res);
		goto EXIT;
	}

	// Promote
	res = promote(PACKAGE_DIR);
	if (res < 0) {
		closeWaitDialog();
		errorDialog(res);
		goto EXIT;
	}

	// Set progress to 100%
	sceMsgDialogProgressBarSetValue(SCE_MSG_DIALOG_PROGRESSBAR_TARGET_BAR_DEFAULT, 100);
	sceKernelDelayThread(COUNTUP_WAIT);
    // Close
    closeWaitDialog();


    //在这里拷贝同目录下的同名目录内所有内容到app目录下
    strcpy(src_path, args->file);
    getVpkDir(src_path);



    /** 打印app所在目录*
    initMessageDialog(SCE_MSG_DIALOG_BUTTON_TYPE_YESNO,src_path);
    dialog_step = DIALOG_STEP_INSTALL_WARNING;
    // Wait for response
    while (dialog_step == DIALOG_STEP_INSTALL_WARNING) {
        sceKernelDelayThread(1000);
    }
    */

    //2. 防止移动app等目录
    //如果发现在ux下并且是一级目录则不复制
    int countSlash = 0;

    int len = strlen(src_path);


    int i;
    for (i = 0; i < len-1; i++) {
        if(src_path[i] == '/')
            countSlash++;
    }

    if(countSlash>0)
    {
        FileList copy_list;
        memset(&copy_list, 0, sizeof(FileList));
        res = fileListGetEntries(&copy_list,src_path);

        //文件夹不存在，不做
        if (res >= 0) {
             //将同目录下与vpk同名的文件夹作为待移动目录
            char src_file_path[MAX_PATH_LENGTH], dst_path[MAX_PATH_LENGTH];

            FileListEntry *copy_entry = NULL;

            copy_entry = copy_list.head;

            int i;
            for (i = 0; i < copy_list.length; i++) {

                if (strcmp(copy_entry->name, ".")!=0 && strcmp(copy_entry->name, "..") != 0)
                {
                    snprintf(src_file_path, MAX_PATH_LENGTH, "%s%s", src_path, copy_entry->name);
                    snprintf(dst_path, MAX_PATH_LENGTH, "%s%s%s%s", "ux0:app/",title_id,"/", copy_entry->name);
                    int res = sceIoRename(src_file_path, dst_path);
                    if (res < 0) {
                        closeWaitDialog();
                        errorDialog(res);
                        goto EXIT;
                    }
                }
                copy_entry = copy_entry->next;
            }

             //1. 安装完成将vpk修改名称,防止再次安装
             if(copy_list.length>1)
             {
                 strcpy(src_path, args->file);
                 getRenameDir(src_path);
                 sceIoRename(args->file, src_path);
             }

        }
        fileListEmpty(&copy_list);



    }




	sceMsgDialogClose();

	free(title_id);

	dialog_step = DIALOG_STEP_INSTALLED;

EXIT:
	if (thid >= 0)
		sceKernelWaitThreadEnd(thid, NULL, NULL);


	// Unlock power timers
	powerUnlock();

	return sceKernelExitDeleteThread(0);
}