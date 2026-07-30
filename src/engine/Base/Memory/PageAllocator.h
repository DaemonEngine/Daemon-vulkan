/*
=============================================================================
Daemon-Vulkan BSD Source Code
Copyright (c) 2025-2026 Reaper
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
	* Redistributions of source code must retain the above copyright
	  notice, this list of conditions and the following disclaimer.
	* Redistributions in binary form must reproduce the above copyright
	  notice, this list of conditions and the following disclaimer in the
	  documentation and/or other materials provided with the distribution.
	* Neither the name of the Reaper nor the
	  names of its contributors may be used to endorse or promote products
	  derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS AS IS AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL REAPER BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
=============================================================================
*/

#ifndef PAGE_ALLOCATOR_H
#define PAGE_ALLOCATOR_H

#include "Int.h"

#include "Allocator.h"

struct AllocRecord {
	static constexpr uint64 HEADER_MAGIC = 0xACC0500D66666666;
	static constexpr uint32 srcSize      = 238;

	std::string Format() const;

	uint64 guardValue = HEADER_MAGIC;
	uint32 size;
	uint16 alignment;
	uint8  memoryArea;
	uint8  threadArea;
	uint8  pageID;

	char   source[srcSize + 1];
};

struct PageRecord {
	uint32 allocSize;
	uint16 allocCount;
};

struct ThreadArea {
	std::atomic<uint64> allocated;
	std::atomic<uint32> allocCount;
	std::atomic<uint32> freeCount;
	uint32              thisThreadAllocCount;
	uint32              thisThreadFreeCount;
	uint64              pad[5];
};

struct MemoryArea {
	byte*  memory;
	uint32 pageSize;
	uint16 threadAreaOffset;
	uint8  threadAreaCount;
	uint8  threadAreaPageCount;
};

struct PageAllocator :
	public Allocator {
	void    Init( const std::string& configText );
	void    Shutdown();

	byte*   Alloc( const uint64 size, const uint64 alignment ) override;
	void    Free( byte* memory ) override;

	private:
	static constexpr uint32 MAX_MEMORY_AREAS = 3;

	MemoryArea  memoryAreas[MAX_MEMORY_AREAS];
	ThreadArea* threadAreas;
	PageRecord* pageAllocs;
	uint64*     acquiredPages;
	uint32      threadAreaOffset;

	uint64* GetAcqPages( const uint8 threadArea );

	byte*   AllocFromPage( const MemoryArea& memoryArea, const uint8 thread, const uint8 pageID,
		                   const uint32 size, const uint16 alignment );
	byte*   AllocFromAcquiredPage( const MemoryArea& memoryArea, const uint32 size, const uint16 alignment );
};

extern PageAllocator pageAllocator;

#endif // PAGE_ALLOCATOR_H