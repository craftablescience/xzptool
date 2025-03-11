// JCalg ultra small, ultra-fast compression / decompression lib.
// Documentation available here: http://bitsum.com/jcalg1.htm
// This library may be used freely in any and for any application.

#pragma once

#include <cstdint>

using JCALG1_FnAlloc = void*(__stdcall*)(uint32_t);
using JCALG1_FnFree = bool(__stdcall*)(void*);
using JCALG1_FnCallback = bool(__stdcall*)(uint32_t, uint32_t);

struct JCALG1_Info {
	uint32_t majorVer;
	uint32_t minorVer;
	uint32_t nFastSize;
	uint32_t nSmallSize;
};

extern "C" uint32_t __stdcall JCALG1_Compress(const void* src, uint32_t srcLen, void* dest, uint32_t windowSize, JCALG1_FnAlloc, JCALG1_FnFree, JCALG1_FnCallback, int32_t bDisableChecksum);
extern "C" uint32_t __stdcall JCALG1_Decompress_Fast(const void* src, void* dest);
extern "C" uint32_t __stdcall JCALG1_Decompress_Small(const void* src, void* dest);
extern "C" uint32_t __stdcall JCALG1_GetNeededBufferSize(uint32_t nSize);
extern "C" uint32_t __stdcall JCALG1_GetInfo(JCALG1_Info* info);
extern "C" uint32_t __stdcall JCALG1_GetUncompressedSizeOfCompressedBlock(const void *pBlock);
extern "C" uint32_t __stdcall JCALG1_GetNeededBufferSize(uint32_t nSize);
