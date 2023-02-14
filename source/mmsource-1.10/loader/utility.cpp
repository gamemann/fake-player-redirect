/**
 * vim: set ts=4 :
 * ======================================================
 * Metamod:Source
 * Copyright (C) 2004-2008 AlliedModders LLC and authors.
 * All rights reserved.
 * ======================================================
 *
 * This software is provided 'as-is', without any express or implied warranty.
 * In no event will the authors be held liable for any damages arising from 
 * the use of this software.
 * 
 * Permission is granted to anyone to use this software for any purpose, 
 * including commercial applications, and to alter it and redistribute it 
 * freely, subject to the following restrictions:
 *
 * 1. The origin of this software must not be misrepresented; you must not 
 * claim that you wrote the original software. If you use this software in a 
 * product, an acknowledgment in the product documentation would be 
 * appreciated but is not required.
 * 2. Altered source versions must be plainly marked as such, and must not be
 * misrepresented as being the original software.
 * 3. This notice may not be removed or altered from any source distribution.
 *
 * Version: $Id$
 */

#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdlib.h>
#include "loader.h"
#include "utility.h"

#if defined __linux__
#include <link.h>

#define PAGE_SIZE			4096
#define PAGE_ALIGN_UP(x)	((x + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1))
#elif defined __APPLE__
#include <mach-o/loader.h>
#endif

#if defined _WIN32
static void
mm_GetPlatformError(char *buffer, size_t maxlength)
{
	DWORD dw = GetLastError();
	FormatMessageA(
		FORMAT_MESSAGE_FROM_SYSTEM|FORMAT_MESSAGE_IGNORE_INSERTS,
		NULL,
		dw,
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		(LPSTR)buffer,
		maxlength,
		NULL);
}
#endif


size_t
mm_FormatArgs(char *buffer, size_t maxlength, const char *fmt, va_list params)
{
	size_t len = vsnprintf(buffer, maxlength, fmt, params);

	if (len >= maxlength)
	{
		len = maxlength - 1;
		buffer[len] = '\0';
	}

	return len;
}

size_t
mm_Format(char *buffer, size_t maxlength, const char *fmt, ...)
{
	size_t len;
	va_list ap;

	va_start(ap, fmt);
	len = mm_FormatArgs(buffer, maxlength, fmt, ap);
	va_end(ap);

	return len;
}

size_t
mm_PathFormat(char *buffer, size_t maxlen, const char *fmt, ...)
{
	size_t len;
	va_list ap;

	va_start(ap, fmt);
	len = mm_FormatArgs(buffer, maxlen, fmt, ap);
	va_end(ap);

	for (size_t i = 0; i < len; i++)
	{
		if (buffer[i] == ALT_SEP_CHAR)
			buffer[i] = PATH_SEP_CHAR;
	}

	return len;
}

void
mm_TrimLeft(char *buffer)
{
	/* Let's think of this as our iterator */
	char *i = buffer;

	/* Make sure the buffer isn't null */
	if (i && *i)
	{
		/* Add up number of whitespace characters */
		while(isspace((unsigned char) *i))
			i++;

		/* If whitespace chars in buffer then adjust string so first non-whitespace char is at start of buffer */
		if (i != buffer)
			memmove(buffer, i, (strlen(i) + 1) * sizeof(char));
	}
}

void
mm_TrimRight(char *buffer)
{
	/* Make sure buffer isn't null */
	if (buffer)
	{
		size_t len = strlen(buffer);

		/* Loop through buffer backwards while replacing whitespace chars with null chars */
		for (size_t i = len - 1; i < len; i--)
		{
			if (isspace((unsigned char) buffer[i]))
				buffer[i] = '\0';
			else
				break;
		}
	}
}

/* :TODO: this should skip string literals */
void
mm_TrimComments(char *buffer)
{
	int num_sc = 0;
	size_t len = strlen(buffer);
	if (buffer)
	{
		for (int i = len - 1; i >= 0; i--)
		{
			if (buffer[i] == '/')
			{
				if (++num_sc >= 2 && i==0)
				{
					buffer[i] = '\0';
					return;
				}
			}
			else
			{
				if (num_sc >= 2)
				{
					buffer[i] = '\0';
					return;
				}
				num_sc = 0;
			}
			/* size_t won't go below 0, manually break out */
			if (i == 0)
				break;
			
		}
	}
}

void
mm_KeySplit(const char *str, char *buf1, size_t len1, char *buf2, size_t len2)
{
	size_t start;
	size_t len = strlen(str);

	for (start = 0; start < len; start++)
	{
		if (!isspace(str[start]))
			break;
	}

	size_t end;
	for (end = start; end < len; end++)
	{
		if (isspace(str[end]))
			break;
	}
	
	size_t i, c = 0;
	for (i = start; i < end; i++, c++)
	{
		if (c >= len1)
			break;
		buf1[c] = str[i];
	}
	buf1[c] = '\0';

	for (start = end; start < len; start++)
	{
		if (!isspace(str[start]))
			break;
	}

	for (c = 0; start < len; start++, c++)
	{
		if (c >= len2)
			break;
		buf2[c] = str[start];
	}
	buf2[c] = '\0';
}

bool
mm_PathCmp(const char *path1, const char *path2)
{
	size_t pos1 = 0, pos2 = 0;

	while (true)
	{
		if (path1[pos1] == '\0' || path2[pos2] == '\0')
			return (path1[pos1] == path2[pos2]);

		if (path1[pos1] == PATH_SEP_CHAR)
		{
			if (path2[pos2] != PATH_SEP_CHAR)
				return false;

			/* Look for extra path chars */
			while (path1[++pos1])
			{
				if (path1[pos1] != PATH_SEP_CHAR)
					break;
			}
			while (path2[++pos2])
			{
				if (path2[pos2] != PATH_SEP_CHAR)
					break;
			}
			continue;
		}

		/* If we're at a different non-alphanumeric, the next character MUST match */
		if ((((unsigned)path1[pos1] & 0x80) && path1[pos1] != path2[pos2])
			||
			(!isalpha(path1[pos1]) && (path1[pos1] != path2[pos2]))
			)
		{
			return false;
		}

	#ifdef WIN32
		if (toupper(path1[pos1]) != toupper(path2[pos2]))
	#else
		if (path1[pos1] != path2[pos2])
	#endif
		{
			return false;
		}

		pos1++;
		pos2++;
	}
}

bool
mm_ResolvePath(const char *path, char *buffer, size_t maxlength)
{
#if defined _WIN32
	return _fullpath(buffer, path, maxlength) != NULL;
#elif defined __linux__ || defined __APPLE__
	assert(maxlength >= PATH_MAX);
	return realpath(path, buffer) != NULL;
#endif
}

void *
mm_LoadLibrary(const char *path, char *buffer, size_t maxlength)
{
	void *lib;

#if defined _WIN32
	lib = (void*)LoadLibrary(path);

	if (lib == NULL)
	{
		mm_GetPlatformError(buffer, maxlength);
		return NULL;
	}
#elif defined __linux__ || defined __APPLE__
	lib = dlopen(path, RTLD_NOW);

	if (lib == NULL)
	{
		mm_Format(buffer, maxlength, "%s", dlerror());
		return NULL;
	}
#endif

	return lib;
}

void *
mm_GetLibAddress(void *lib, const char *name)
{
#if defined _WIN32
	return GetProcAddress((HMODULE)lib, name);
#elif defined __linux__ || defined __APPLE__
	return dlsym(lib, name);
#endif
}

void
mm_UnloadLibrary(void *lib)
{
#if defined _WIN32
	FreeLibrary((HMODULE)lib);
#elif defined __linux__ || defined __APPLE__
	dlclose(lib);
#endif
}

bool
mm_GetFileOfAddress(void *pAddr, char *buffer, size_t maxlength)
{
#if defined _WIN32
	MEMORY_BASIC_INFORMATION mem;
	if (!VirtualQuery(pAddr, &mem, sizeof(mem)))
		return false;
	if (mem.AllocationBase == NULL)
		return false;
	HMODULE dll = (HMODULE)mem.AllocationBase;
	GetModuleFileName(dll, (LPTSTR)buffer, maxlength);
#elif defined __linux__ || defined __APPLE__
	Dl_info info;
	if (!dladdr(pAddr, &info))
		return false;
	if (!info.dli_fbase || !info.dli_fname)
		return false;
	const char *dllpath = info.dli_fname;
	snprintf(buffer, maxlength, "%s", dllpath);
#endif
	return true;
}

struct DynLibInfo
{
	void *baseAddress;
	size_t memorySize;
};

static bool
mm_GetLibraryInfo(const void *libPtr, DynLibInfo &lib)
{
	uintptr_t baseAddr;

	if (libPtr == NULL)
	{
		return false;
	}

#ifdef _WIN32

	MEMORY_BASIC_INFORMATION info;
	IMAGE_DOS_HEADER *dos;
	IMAGE_NT_HEADERS *pe;
	IMAGE_FILE_HEADER *file;
	IMAGE_OPTIONAL_HEADER *opt;

	if (!VirtualQuery(libPtr, &info, sizeof(MEMORY_BASIC_INFORMATION)))
	{
		return false;
	}

	baseAddr = reinterpret_cast<uintptr_t>(info.AllocationBase);

	/* All this is for our insane sanity checks :o */
	dos = reinterpret_cast<IMAGE_DOS_HEADER *>(baseAddr);
	pe = reinterpret_cast<IMAGE_NT_HEADERS *>(baseAddr + dos->e_lfanew);
	file = &pe->FileHeader;
	opt = &pe->OptionalHeader;

	/* Check PE magic and signature */
	if (dos->e_magic != IMAGE_DOS_SIGNATURE || pe->Signature != IMAGE_NT_SIGNATURE || opt->Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC)
	{
		return false;
	}

	/* Check architecture, which is 32-bit/x86 right now
	* Should change this for 64-bit if Valve gets their act together
	*/
	if (file->Machine != IMAGE_FILE_MACHINE_I386)
	{
		return false;
	}

	/* For our purposes, this must be a dynamic library */
	if ((file->Characteristics & IMAGE_FILE_DLL) == 0)
	{
		return false;
	}

	/* Finally, we can do this */
	lib.memorySize = opt->SizeOfImage;

#elif defined __linux__

	Dl_info info;
	Elf32_Ehdr *file;
	Elf32_Phdr *phdr;
	uint16_t phdrCount;

	if (!dladdr(libPtr, &info))
	{
		return false;
	}

	if (!info.dli_fbase || !info.dli_fname)
	{
		return false;
	}

	/* This is for our insane sanity checks :o */
	baseAddr = reinterpret_cast<uintptr_t>(info.dli_fbase);
	file = reinterpret_cast<Elf32_Ehdr *>(baseAddr);

	/* Check ELF magic */
	if (memcmp(ELFMAG, file->e_ident, SELFMAG) != 0)
	{
		return false;
	}

	/* Check ELF version */
	if (file->e_ident[EI_VERSION] != EV_CURRENT)
	{
		return false;
	}

	/* Check ELF architecture, which is 32-bit/x86 right now
	* Should change this for 64-bit if Valve gets their act together
	*/
	if (file->e_ident[EI_CLASS] != ELFCLASS32 || file->e_machine != EM_386 || file->e_ident[EI_DATA] != ELFDATA2LSB)
	{
		return false;
	}

	/* For our purposes, this must be a dynamic library/shared object */
	if (file->e_type != ET_DYN)
	{
		return false;
	}

	phdrCount = file->e_phnum;
	phdr = reinterpret_cast<Elf32_Phdr *>(baseAddr + file->e_phoff);

	for (uint16_t i = 0; i < phdrCount; i++)
	{
		Elf32_Phdr &hdr = phdr[i];

		/* We only really care about the segment with executable code */
		if (hdr.p_type == PT_LOAD && hdr.p_flags == (PF_X|PF_R))
		{
			/* From glibc, elf/dl-load.c:
			 * c->mapend = ((ph->p_vaddr + ph->p_filesz + GLRO(dl_pagesize) - 1)
			 * & ~(GLRO(dl_pagesize) - 1));
			 *
			 * In glibc, the segment file size is aligned up to the nearest page size and
			 * added to the virtual address of the segment. We just want the size here.
			 */
			lib.memorySize = PAGE_ALIGN_UP(hdr.p_filesz);
			break;
		}
	}

#elif defined __APPLE__

	Dl_info info;
	struct mach_header *file;
	struct segment_command *seg;
	uint32_t cmd_count;

	if (!dladdr(libPtr, &info))
	{
		return false;
	}

	if (!info.dli_fbase || !info.dli_fname)
	{
		return false;
	}

	/* This is for our insane sanity checks :o */
	baseAddr = (uintptr_t)info.dli_fbase;
	file = (struct mach_header *)baseAddr;

	/* Check Mach-O magic */
	if (file->magic != MH_MAGIC)
	{
		return false;
	}

	/* Check architecture (32-bit/x86) */
	if (file->cputype != CPU_TYPE_I386 || file->cpusubtype != CPU_SUBTYPE_I386_ALL)
	{
		return false;
	}

	/* For our purposes, this must be a dynamic library */
	if (file->filetype != MH_DYLIB)
	{
		return false;
	}

	cmd_count = file->ncmds;
	seg = (struct segment_command *)(baseAddr + sizeof(struct mach_header));

	/* Add up memory sizes of mapped segments */
	for (uint32_t i = 0; i < cmd_count; i++)
	{
		if (seg->cmd == LC_SEGMENT)
		{
			lib.memorySize += seg->vmsize;
		}

		seg = (struct segment_command *)((uintptr_t)seg + seg->cmdsize);
	}

#endif

	lib.baseAddress = reinterpret_cast<void *>(baseAddr);

	return true;
}

void *mm_FindPattern(const void *libPtr, const char *pattern, size_t len)
{
	DynLibInfo lib;
	bool found;
	char *ptr, *end;

	memset(&lib, 0, sizeof(DynLibInfo));

	if (!mm_GetLibraryInfo(libPtr, lib))
	{
		return NULL;
	}

	ptr = reinterpret_cast<char *>(lib.baseAddress);
	end = ptr + lib.memorySize - len;

	while (ptr < end)
	{
		found = true;
		for (register size_t i = 0; i < len; i++)
		{
			if (pattern[i] != '\x2A' && pattern[i] != ptr[i])
			{
				found = false;
				break;
			}
		}

		if (found)
			return ptr;

		ptr++;
	}

	return NULL;
}
