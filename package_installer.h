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

#ifndef __PACKAGE_INSTALLER_H__
#define __PACKAGE_INSTALLER_H__

typedef struct SfoHeader {
	uint32_t magic;
	uint32_t version;
	uint32_t keyofs;
	uint32_t valofs;
	uint32_t count;
} __attribute__((packed)) SfoHeader;

typedef struct SfoEntry {
	uint16_t nameofs;
	uint8_t  alignment;
	uint8_t  type;
	uint32_t valsize;
	uint32_t totalsize;
	uint32_t dataofs;
} __attribute__((packed)) SfoEntry;

typedef struct {
	char *file;
} InstallArguments;

int install_thread(SceSize args_size, InstallArguments *args);


#endif