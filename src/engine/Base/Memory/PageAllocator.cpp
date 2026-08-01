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
#include "AlignedAtomic.h"
#include "Parser.h"

#include "SysAllocator.h"

#include "PageAllocator.h"

struct PageAllocator::AllocRecord {
	static constexpr uint64 HEADER_MAGIC = 0xACC0500D66666666;
	static constexpr uint32 srcSize      = 230;

	std::string Format() const;

	uint64 guardValue = HEADER_MAGIC;
	uint32 size;
	uint16 alignment;
	uint8  memoryArea;
	uint8  threadArea;
	uint8  allocatorThread;
	uint8  pageID;

	char   source[srcSize + 1];
};

std::string PageAllocator::AllocRecord::Format() const {
	if ( guardValue == HEADER_MAGIC ) {
		return Str::Format( "guard value: %u, size: %u, alignment: %u, chunkID: %u, source: %s",
			guardValue, size, alignment, pageID,
			source );
	}

	return Str::Format( "guard value: %u (corrupted, should be: %u), size: %u, alignment: %u, chunkID: %u, source: %s",
		guardValue, HEADER_MAGIC, size, alignment, pageID,
		source );
}

struct PageAllocator::PageRecord {
	uint32 allocSize;
	uint16 allocCount;
};

struct PageAllocator::ThreadArea {
	std::atomic<uint64> allocated;
	std::atomic<uint32> allocCount;
	std::atomic<uint32> freeCount;
	uint32              thisThreadAllocCount;
	uint32              thisThreadFreeCount;
	uint32              pageOffset;
	uint32              memoryOffset;
	uint32              pad[8];
};

static       bool   pageAllocatorActive = false;
thread_local uint64 acquiredThreadAreas[PageAllocator::MAX_MEMORY_AREAS];

void PageAllocator::Init( const std::string& configText ) {
	Log::Notice( "Parsing pageConfig: %s", configText );

	StringView v { configText.c_str(), configText.size() };

	uint8 memoryAreaCount  = 0;
	totalThreadAreas       = 0;
	totalSharedThreadAreas = 0;
	totalPageCount         = 0;
	totalSharedPageCount   = 0;

	do {
		StringView o = Parse( v );

		if ( memoryAreaCount >= MAX_MEMORY_AREAS ) {
			Log::Warn( "Config has more memory areas specified than the maximum (%u)", MAX_MEMORY_AREAS );

			break;
		}

		int threadAreaCount;
		int pageCount;
		int pageSize;

		const bool shared = o == "s";

		o = Parse( v ); // :

		o = Parse( v );
		Q_strtoi( o.memory, &threadAreaCount );

		o = Parse( v ); // :

		o = Parse( v );
		Q_strtoi( o.memory, &pageCount );

		o = Parse( v ); // :

		o = Parse( v );
		Q_strtoi( o.memory, &pageSize );

		pageSize       *= 1024;
		threadAreaCount = threadAreaCount ? threadAreaCount : CPU_CORES;

		if ( threadAreaCount <= 0 || threadAreaCount >= 64 ) {
			Log::Warn( "Bad threadAreaCount: %u, must be in (0, 63]", threadAreaCount );
			threadAreaCount = 64;
		}

		if ( pageCount <= 0 || pageCount >= 64 ) {
			Log::Warn( "Bad pageCount: %u, must be in (0, 63]", pageCount );
			pageCount = 64;
		}

		if ( pageSize < MIN_PAGE_SIZE || pageSize > MAX_PAGE_SIZE ) {
			Log::Warn( "Bad pageSize: %u, must be in [%u, 63]", pageSize );
			pageSize = MIN_PAGE_SIZE;
		}

		uint32 size = pageSize * pageCount * threadAreaCount;

		memoryAreas[memoryAreaCount] = {
			.memory              = sysAllocator.Alloc( size, 64 ),
			.pageSize            = ( uint32 ) pageSize,
			.threadAreaOffset    = ( uint16 ) totalThreadAreas,
			.threadAreaCount     = ( uint8 )  threadAreaCount,
			.threadAreaPageCount = ( uint8 )  pageCount,
			.id                  = 0,
			.shared              = shared
		};

		memoryAreaCount++;
		totalThreadAreas += threadAreaCount;
		totalPageCount   += pageCount * threadAreaCount;

		if ( shared ) {
			totalSharedThreadAreas += threadAreaCount;
			totalSharedPageCount   += pageCount * threadAreaCount;
		}
	} while ( v.size );

	std::sort( memoryAreas, memoryAreas + memoryAreaCount,
		[]( const MemoryArea& lhs, const MemoryArea& rhs ) {
			if ( lhs.shared != rhs.shared ) {
				return !lhs.shared;
			}

			return lhs.pageSize < rhs.pageSize;
		}
	);

	acquiredPagesStride = PAD( totalThreadAreas * sizeof( uint64 ), CACHE_LINE_SIZE ) / CACHE_LINE_SIZE;

	threadAreas         = ( ThreadArea* )          sysAllocator.Alloc( totalThreadAreas                   * sizeof( ThreadArea ),          64 );
	pageAllocs          = ( PageRecord* )          sysAllocator.Alloc( totalPageCount                     * sizeof( PageRecord ),          64 );
	sharedPageAllocs    = ( AlignedAtomicUint32* ) sysAllocator.Alloc( totalSharedPageCount               * sizeof( AlignedAtomicUint32 ), 64 );
	acquiredPages       = ( uint64* )              sysAllocator.Alloc( CPU_CORES * acquiredPagesStride    * sizeof( uint64 ),              64 );
	freedSharedPages    = ( AlignedAtomicUint64* ) sysAllocator.Alloc( CPU_CORES * totalSharedThreadAreas * sizeof( AlignedAtomicUint64 ), 64 );

	uint32 pageOffset = 0;

	for ( MemoryArea& memoryArea : memoryAreas ) {
		uint32 memoryOffset = 0;
		memoryArea.id       = &memoryArea - memoryAreas;

		for ( uint16 thread = memoryArea.threadAreaOffset; thread < memoryArea.threadAreaCount + memoryArea.threadAreaOffset; thread++ ) {
			threadAreas[thread].pageOffset   = pageOffset;
			threadAreas[thread].memoryOffset = memoryOffset;

			pageOffset                      += memoryArea.threadAreaPageCount;
			memoryOffset                    += memoryArea.threadAreaPageCount * memoryArea.pageSize;
		}
	}

	Log::Notice( "Parsed pageConfig" );

	pageAllocatorActive = true;
}

void PageAllocator::Shutdown() {
	pageAllocatorActive = false;

	for ( const MemoryArea& memoryArea : memoryAreas ) {
		Log::Notice( "Memory area %u: %s, areas: %u, pages per area: %u, page size: %u, total memory size: %u",
			&memoryArea - memoryAreas, memoryArea.shared ? "shared" : "local",
			memoryArea.threadAreaCount, memoryArea.threadAreaPageCount, memoryArea.pageSize,
			memoryArea.threadAreaCount * memoryArea.threadAreaPageCount * memoryArea.pageSize );

		for ( uint8 i = 0; i < memoryArea.threadAreaCount; i++ ) {
			ThreadArea& threadArea = threadAreas[i + memoryArea.threadAreaOffset];

			Log::Notice( "ThreadArea %u: allocated pages: %u, total allocs: %u, allocs from assigned thread: %u",
				i, CountBits( threadArea.allocated.load( std::memory_order_relaxed ) ), threadArea.allocCount, threadArea.thisThreadAllocCount );
		}

		sysAllocator.Free( memoryArea.memory );
	}

	sysAllocator.Free( ( byte* ) threadAreas );
	sysAllocator.Free( ( byte* ) pageAllocs );
	sysAllocator.Free( ( byte* ) sharedPageAllocs );
	sysAllocator.Free( ( byte* ) acquiredPages );
	sysAllocator.Free( ( byte* ) freedSharedPages );
}

uint64* PageAllocator::GetAcqPages( const uint8 threadArea ) {
	return acquiredPages    + threadArea + TLM.id * acquiredPagesStride;
}

AlignedAtomicUint64* PageAllocator::GetFreedSharedPages( const uint8 allocatorThread, const uint8 threadArea ) {
	return freedSharedPages + threadArea + allocatorThread * totalSharedThreadAreas;
}

byte* PageAllocator::AllocFromPage( const MemoryArea& memoryArea, const uint8 thread, const uint8 pageID,
	                                const uint32 size, const uint16 alignment ) {
	const uint16 globalThread = thread + memoryArea.threadAreaOffset;
	ThreadArea&  threadArea   = threadAreas[globalThread];

	const uint32 globalPageID = threadArea.pageOffset + pageID;
	PageRecord&  page         = pageAllocs[globalPageID];

	threadArea.allocCount.fetch_add( 1, std::memory_order_relaxed );

	if ( thread == TLM.id % memoryArea.threadAreaCount ) {
		threadArea.thisThreadAllocCount++;
	}

	byte*       memory  = memoryArea.memory + threadArea.memoryOffset + pageID * memoryArea.pageSize + page.allocSize;

	std::string source = FormatSrc( std::stacktrace::current(), true, true );

	AllocRecord record {
		.size            = size,
		.alignment       = alignment,
		.memoryArea      = memoryArea.id,
		.threadArea      = thread,
		.allocatorThread = ( uint8 ) TLM.id,
		.pageID          = pageID
	};

	Q_strncpyz( record.source, source.c_str(), AllocRecord::srcSize );
	record.source[AllocRecord::srcSize] = '\0';

	*( AllocRecord* ) memory = record;

	page.allocCount++;
	page.allocSize += size;

	if ( memoryArea.shared ) {
		sharedPageAllocs[globalPageID - ( totalPageCount - totalSharedPageCount )].value.fetch_add( 1, std::memory_order_relaxed );
	}

	SetBit( acquiredThreadAreas + memoryArea.id, thread );
	SetBit( GetAcqPages( globalThread ),         pageID );

	return memory + sizeof( AllocRecord );
}

byte* PageAllocator::AllocFromAcquiredPage( const MemoryArea& memoryArea, const uint32 size, const uint16 alignment ) {
	uint64 acqThreadAreas = acquiredThreadAreas[memoryArea.id];

	while ( acqThreadAreas ) {
		uint32 thread       = FindLSB( acqThreadAreas );
		uint32 globalThread = thread + memoryArea.threadAreaOffset;

		uint64 acqPages     = *GetAcqPages( globalThread );

		while ( acqPages ) {
			uint32       pageID       = FindLSB( acqPages );
			ThreadArea&  threadArea   = threadAreas[globalThread];
			const uint32 globalPageID = threadArea.pageOffset + pageID;

			PageRecord&  page         = pageAllocs[globalPageID];
			
			if ( page.allocSize + size <= memoryArea.pageSize ) {
				return AllocFromPage( memoryArea, thread, pageID, size, alignment );
			}

			UnSetBit( &acqPages, pageID );
		}

		UnSetBit( &acqThreadAreas, thread );
	}

	return nullptr;
}

byte* PageAllocator::Alloc( const uint64 size, const uint64 alignment, const bool shared ) {
	if ( !size ) [[unlikely]] {
		return nullptr;
	}

	const uint32 paddedSize = ( size + sizeof( AllocRecord ) + alignment - 1 ) & ~( alignment - 1 );

	for ( const MemoryArea& memoryArea : memoryAreas ) {
		if ( size > memoryArea.pageSize ) {
			continue;
		}

		if ( shared != memoryArea.shared ) {
			continue;
		}

		if ( byte* memory = AllocFromAcquiredPage( memoryArea, paddedSize, alignment ) ) {
			return memory;
		}

		for ( uint8 i = 0; i < memoryArea.threadAreaCount; i++ ) {
			uint8       thread       = ( i + TLM.id ) % memoryArea.threadAreaCount;
			uint8       globalThread = thread + memoryArea.threadAreaOffset;
			ThreadArea& threadArea   = threadAreas[globalThread];

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

byte* PageAllocator::Alloc( const uint64 size, const uint64 alignment ) {
	return Alloc( size, alignment, false );
}

byte* PageAllocator::AllocShared( const uint64 size, const uint64 alignment ) {
	return Alloc( size, alignment, true );
}

void PageAllocator::ClearPage( const MemoryArea& memoryArea, const uint8 thread, const uint8 pageID ) {
	const uint16 globalThread = memoryArea.threadAreaOffset + thread;
	ThreadArea&  threadArea   = threadAreas[globalThread];
	const uint32 globalPageID = threadArea.pageOffset + pageID;

	PageRecord&  page         = pageAllocs[globalPageID];
	page.allocCount           = 0;
	page.allocSize            = 0;

	uint64* acqPages = GetAcqPages( globalThread );

	UnSetBit( acqPages, pageID );

	if ( !*( acqPages ) ) {
		UnSetBit( acquiredThreadAreas + memoryArea.id, thread );
	}

	threadAreas[globalThread].allocated.fetch_sub( SetBit( 0ull, pageID ), std::memory_order_relaxed );
}

void PageAllocator::Free( byte* memory ) {
	if ( !pageAllocatorActive ) [[unlikely]] {
		return;
	}

	AllocRecord* record = ( AllocRecord* ) ( memory - sizeof( AllocRecord ) );

	if ( record->guardValue != AllocRecord::HEADER_MAGIC ) {
		Err( "Page corrupted: %s", record->Format() );
	}

	const MemoryArea& memoryArea          = memoryAreas[record->memoryArea];
	ThreadArea&       threadArea          = threadAreas[memoryArea.threadAreaOffset + record->threadArea];
	const bool        allocFromThisThread = record->allocatorThread == TLM.id;
	const uint32      globalPageID        = threadArea.pageOffset + record->pageID;

	threadArea.freeCount.fetch_add( 1, std::memory_order_relaxed );

	if ( allocFromThisThread ) {
		threadArea.thisThreadFreeCount++;
	}

	uint16 allocCount;

	if ( memoryArea.shared ) {
		AlignedAtomicUint32& cnt  = sharedPageAllocs[globalPageID - ( totalPageCount - totalSharedPageCount )];
		allocCount                = cnt.value.fetch_sub( 1, std::memory_order_relaxed ) - 1;
	} else {
		PageRecord&          page = pageAllocs[globalPageID];
		allocCount                = page.allocCount--;
	}

	if ( allocCount ) {
		return;
	}

	if ( memoryArea.shared && !allocFromThisThread ) {
		std::atomic<uint64>& acqPages = GetFreedSharedPages( record->allocatorThread, record->threadArea )->value;
		acqPages.fetch_add( SetBit( 0ull, record->pageID ), std::memory_order_relaxed );

		return;
	}

	ClearPage( memoryArea, record->threadArea, record->pageID );
}

void PageAllocator::FreeSharedPages() {
	for ( const MemoryArea& memoryArea : memoryAreas ) {
		if ( !memoryArea.shared ) {
			continue;
		}

		uint64 acqThreadAreas = acquiredThreadAreas[memoryArea.id];

		while ( acqThreadAreas ) {
			uint32               thread       = FindLSB( acqThreadAreas );
			uint32               globalThread = thread + memoryArea.threadAreaOffset;

			std::atomic<uint64>& acqPages     = GetFreedSharedPages( TLM.id, thread )->value;
			uint64               cleared      = acqPages.load( std::memory_order_relaxed );

			acqPages.fetch_sub( cleared, std::memory_order_relaxed );

			while ( cleared ) {
				uint32 pageID = FindLSB( cleared );
				ClearPage( memoryArea, thread, pageID );
				UnSetBit( &cleared, pageID );
			}

			UnSetBit( &acqThreadAreas, thread );
		}
	}
}

PageAllocator pageAllocator;