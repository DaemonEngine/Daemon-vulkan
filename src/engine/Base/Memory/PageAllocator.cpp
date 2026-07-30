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

#include "SrcDebug/StackTrace.h"
#include "Sys/CPUInfo.h"
#include "Thread/ThreadMemory.h"
#include "Bit.h"
#include "Error.h"
#include "Parser.h"

#include "SysAllocator.h"

#include "PageAllocator.h"

constexpr uint32 MAX_MEMORY_AREAS = 3;

static       bool   pageAllocatorActive = false;
thread_local uint64 acquiredThreadAreas[MAX_MEMORY_AREAS];

void PageAllocator::Init( const std::string& configText ) {
	Log::Notice( "Parsing pageConfig: %s", configText );

	StringView v { configText.c_str(), configText.size() };

	uint32 memoryAreaCount  = 0;
	threadAreaOffset        = 0;
	uint32 totalPageCount   = 0;

	do {
		StringView o = Parse( v );

		if ( memoryAreaCount >= MAX_MEMORY_AREAS ) {
			Log::Warn( "Config has more memory areas specified than the maximum (%u)", MAX_MEMORY_AREAS );

			break;
		}

		int threadAreaCount;
		int pageCount;
		int pageSize;

		Q_strtoi( o.memory, &threadAreaCount );

		o = Parse( v ); // :

		o = Parse( v );
		Q_strtoi( o.memory, &pageCount );

		o = Parse( v ); // :

		o = Parse( v );
		Q_strtoi( o.memory, &pageSize );

		if ( threadAreaCount <= 0 || threadAreaCount >= 64 ) {
			Log::Warn( "Bad threadAreaCount: %u, must be in (0, 63]", threadAreaCount );
			threadAreaCount = 64;
		}

		if ( pageCount <= 0 || pageCount >= 64 ) {
			Log::Warn( "Bad pageCount: %u, must be in (0, 63]", pageCount );
			pageCount = 64;
		}

		pageSize       *= 1024;

		if ( pageSize < MIN_PAGE_SIZE || pageSize > MAX_PAGE_SIZE ) {
			Log::Warn( "Bad pageSize: %u, must be in [%u, 63]", pageSize );
			pageSize = MIN_PAGE_SIZE;
		}

		threadAreaCount = threadAreaCount ? threadAreaCount : CPU_CORES;

		uint32 size     = pageSize * pageCount * threadAreaCount;

		memoryAreas[memoryAreaCount] = {
			.memory              = sysAllocator.Alloc( size, 64 ),
			.pageSize            = ( uint32 ) pageSize,
			.threadAreaOffset    = ( uint16 ) threadAreaOffset,
			.threadAreaCount     = ( uint8 )  threadAreaCount,
			.threadAreaPageCount = ( uint8 )  pageCount
		};

		threadAreaOffset += threadAreaCount;
		totalPageCount   += pageCount * threadAreaCount;

		memoryAreaCount++;
	} while ( v.size );

	std::sort( memoryAreas, memoryAreas + MAX_MEMORY_AREAS,
		[]( const MemoryArea& lhs, const MemoryArea& rhs ) {
			return lhs.pageSize < rhs.pageSize;
		}
	);

	threadAreas   = ( ThreadArea* ) sysAllocator.Alloc( threadAreaOffset                  * sizeof( ThreadArea ),   64 );
	pageAllocs    = ( PageRecord* ) sysAllocator.Alloc( totalPageCount                    * sizeof( PageRecord ),   64 );
	acquiredPages = ( uint64* )     sysAllocator.Alloc( CPU_CORES * PAD( threadAreaOffset * sizeof( uint64 ), 64 ), 64 );

	Log::Notice( "Parsed pageConfig" );

	pageAllocatorActive = true;
}

void PageAllocator::Shutdown() {
	pageAllocatorActive = false;

	for ( const MemoryArea& memoryArea : memoryAreas ) {
		Log::Notice( "Memory area %u: areas: %u, pages per area: %u, page size: %u",
			&memoryArea - memoryAreas, memoryArea.threadAreaCount, memoryArea.threadAreaPageCount, memoryArea.pageSize );

		for ( uint8 i = 0; i < memoryArea.threadAreaCount; i++ ) {
			ThreadArea& threadArea = threadAreas[i + memoryArea.threadAreaOffset];

			Log::Notice( "ThreadArea %u: allocated pages: %u, total allocs: %u, allocs from assigned thread: %u",
				i, CountBits( threadArea.allocated.load( std::memory_order_relaxed ) ), threadArea.allocCount, threadArea.thisThreadAllocCount );
		}

		sysAllocator.Free( memoryArea.memory );
	}

	sysAllocator.Free( ( byte* ) threadAreas );
}

uint64* PageAllocator::GetAcqPages( const uint8 threadArea ) {
	return acquiredPages + threadArea + TLM.id * PAD( threadAreaOffset, 64 );
}

byte* PageAllocator::AllocFromPage( const MemoryArea& memoryArea, const uint8 thread, const uint8 pageID,
	                                const uint32 size, const uint16 alignment ) {
	const uint32 globalPageID = thread * memoryArea.threadAreaPageCount + pageID;

	PageRecord&  page       = pageAllocs[globalPageID];
	ThreadArea&  threadArea = threadAreas[thread];

	threadArea.allocCount.fetch_add( 1, std::memory_order_relaxed );

	if ( thread == TLM.id % memoryArea.threadAreaCount ) {
		threadArea.thisThreadAllocCount++;
	}

	byte*       memory  = memoryArea.memory + globalPageID * memoryArea.pageSize + page.allocSize;
	uint8       memArea = &memoryArea - memoryAreas;

	std::string source = FormatSrc( std::stacktrace::current(), true, true );

	AllocRecord record {
		.size       = size,
		.alignment  = alignment,
		.memoryArea = memArea,
		.threadArea = thread,
		.pageID     = pageID
	};

	Q_strncpyz( record.source, source.c_str(), AllocRecord::srcSize );
	record.source[AllocRecord::srcSize] = '\0';

	*( AllocRecord* ) memory = record;

	page.allocCount++;
	page.allocSize += size;

	SetBit( acquiredThreadAreas + memArea, thread );
	SetBit( GetAcqPages( thread ),         pageID );

	return memory + sizeof( AllocRecord );
}

byte* PageAllocator::AllocFromAcquiredPage( const MemoryArea& memoryArea, const uint32 size, const uint16 alignment ) {
	uint64 acqThreadAreas = acquiredThreadAreas[&memoryArea - memoryAreas];

	while ( acqThreadAreas ) {
		uint32 offset   = FindLSB( acqThreadAreas );
		uint32 thread   = offset + memoryArea.threadAreaOffset;

		uint64 acqPages = *GetAcqPages( thread );

		while ( acqPages ) {
			uint32      pageID = FindLSB( acqPages );
			PageRecord& page   = pageAllocs[thread * memoryArea.threadAreaPageCount + pageID];
			
			if ( page.allocSize + size <= memoryArea.pageSize ) {
				return AllocFromPage( memoryArea, thread, pageID, size, alignment );
			}

			UnSetBit( &acqPages, pageID );
		}

		UnSetBit( &acqThreadAreas, offset );
	}

	return nullptr;
}

byte* PageAllocator::Alloc( const uint64 size, const uint64 alignment ) {
	if ( !size ) {
		return nullptr;
	}

	const uint32 paddedSize = ( size + sizeof( AllocRecord ) + alignment - 1 ) & ~( alignment - 1 );

	for ( const MemoryArea& memoryArea : memoryAreas ) {
		if ( size > memoryArea.pageSize ) {
			continue;
		}

		if ( byte* memory = AllocFromAcquiredPage( memoryArea, paddedSize, alignment ) ) {
			return memory;
		}

		for ( uint8 i = 0; i < memoryArea.threadAreaCount; i++ ) {
			uint8       thread     = ( i + TLM.id ) % memoryArea.threadAreaCount + memoryArea.threadAreaOffset;
			ThreadArea& threadArea = threadAreas[thread];

			uint64 expectedAlloc = threadArea.allocated.load( std::memory_order_relaxed );
			uint64 desired;
			uint16 pageID;
			bool   success       = true;

			do {
				pageID = FindLZeroBit( expectedAlloc );

				if ( pageID >= memoryArea.threadAreaPageCount ) {
					success = false;
					break;
				}

				desired = SetBit( expectedAlloc, pageID );
			} while ( !threadArea.allocated.compare_exchange_strong( expectedAlloc, desired, std::memory_order_relaxed ) );

			if ( !success ) {
				continue;
			}

			return AllocFromPage( memoryArea, thread, pageID, paddedSize, alignment );
		}
	}

	return nullptr;
}

void PageAllocator::Free( byte* memory ) {
	if ( !pageAllocatorActive ) [[unlikely]] {
		return;
	}

	AllocRecord* record = ( AllocRecord* ) ( memory - sizeof( AllocRecord ) );

	if ( record->guardValue != AllocRecord::HEADER_MAGIC ) {
		Err( "Memory chunk corrupted: %s", record->Format() );
	}

	PageRecord& page       = pageAllocs[record->threadArea * memoryAreas[record->memoryArea].threadAreaPageCount + record->pageID];
	ThreadArea& threadArea = threadAreas[record->threadArea];

	page.allocCount--;

	threadArea.freeCount.fetch_add( 1, std::memory_order_relaxed );

	if ( record->threadArea == TLM.id % memoryAreas[record->memoryArea].threadAreaCount ) {
		threadArea.thisThreadFreeCount++;
	}

	if ( !page.allocCount ) {
		uint64* acqPages = GetAcqPages( record->threadArea );

		UnSetBit( acqPages, record->pageID );

		if ( !*( acqPages ) ) {
			UnSetBit( acquiredThreadAreas + record->memoryArea, record->threadArea );
		}

		threadArea.allocated.fetch_sub( SetBit( 0ull, record->pageID ), std::memory_order_relaxed );
	}
}

std::string AllocRecord::Format() const {
	if ( guardValue == HEADER_MAGIC ) {
		return Str::Format( "guard value: %u, size: %u, alignment: %u, chunkID: %u, source: %s",
			guardValue, size, alignment, pageID,
			source );
	}

	return Str::Format( "guard value: %u (corrupted, should be: %u), size: %u, alignment: %u, chunkID: %u, source: %s",
		guardValue, HEADER_MAGIC, size, alignment, pageID,
		source );
}

PageAllocator pageAllocator;