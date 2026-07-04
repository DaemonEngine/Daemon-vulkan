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

#include "common/Common.h"
#include "qcommon/qcommon.h"

#include "engine/framework/CvarSystem.h"
#include "engine/framework/System.h"

#include "Thread/GlobalMemory.h"
#include "Thread/TaskList.h"
#include "Thread/ThreadMemory.h"
#include "Sys/OSLoad.h"
#include "BaseCVars.h"
#include "MemoryChunkSystem.h"
#include "SysAllocator.h"
 
#include "../RefAPI.h"

#include "Surface/Surface.h"

#include "GraphicsCore/Init.h"
#include "GraphicsCore/GraphicsCoreStore.h"

static void InitTLM() {
	TLM.Init();
}

static FenceMain cntFence;

void TestTask() {
	uint64       out = UINT64_MAX;

	volatile int tmp = 0;

	for ( uint32 i = 0; i < 1; i++ ) {
		Timer t;

		__m128 x = _mm_setzero_ps();

		for ( uint32 j = 0; j < 100000; j++ ) {
			x = _mm_add_ps( x, _mm_set1_ps( 1.0f ) );
		}

		tmp += _mm_cvt_ss2si( x );

		out = t.Time() < out ? t.Time() : out;
	}

	cntFence.Signal();
}

void Init( WindowConfig* windowConfig ) {
	OSLoad();

	sysAllocator.Init();
	taskList.Init();

	std::string cfg = e_memoryChunkConfig.Get();

	Task initMemTask { &InitMemoryChunkSystemConfig, cfg };
	Task initSMTask  { &InitGlobalMemory };

	Task initTLMTask { &InitTLM };
	taskList.AddTasks( { initSMTask, initMemTask }, { initTLMTask.ThreadMaskAll(), initMemTask } );

	mainSurface.Init();

	windowConfig->displayWidth  = mainSurface.width;
	windowConfig->displayHeight = mainSurface.height;
	windowConfig->displayAspect = ( float ) windowConfig->displayWidth / windowConfig->displayHeight;
	windowConfig->vidWidth      = mainSurface.screenWidth;
	windowConfig->vidHeight     = mainSurface.screenHeight;

	IN_Init( mainSurface.window );

	initTLMTask.Wait();

	Log::Notice( "Large page size: %u", memoryInfo.PAGE_SIZE_LARGE );

	Cvar::Latch( e_memoryPageSize );

	Task initGraphicsEngineTask { &InitGraphicsEngine };

	taskList.AddTasks( { initGraphicsEngineTask } );

	std::this_thread::sleep_for( std::chrono::microseconds( 3000000 ) );

	Task t { &TestTask };
	Task t2 { &TestTask };
	Task t3 { &TestTask };
	Task t4 { &TestTask };
	Task t5 { &TestTask };
	Task t6 { &TestTask };
	Task t7 { &TestTask };
	Task t8 { &TestTask };
	Task t9 { &TestTask };
	Task t10 { &TestTask };
	Task t11 { &TestTask };
	Task t12 { &TestTask };
	Task t13 { &TestTask };
	Task t14 { &TestTask };
	Task t15 { &TestTask };
	Task t16 { &TestTask };
	Task t17 { &TestTask };
	Task t18 { &TestTask };
	Task t19 { &TestTask };
	Task t20 { &TestTask };
	Task ta { &TestTask };
	Task t2a { &TestTask };
	Task t3a { &TestTask };
	Task t4a { &TestTask };
	Task t5a { &TestTask };
	Task t6a { &TestTask };
	Task t7a { &TestTask };
	Task t8a { &TestTask };
	Task t9a { &TestTask };
	Task t10a { &TestTask };
	Task t11a { &TestTask };
	Task t12a { &TestTask };
	Task t13a { &TestTask };
	Task t14a { &TestTask };
	Task t15a { &TestTask };
	Task t16a { &TestTask };
	Task t17a { &TestTask };
	Task t18a { &TestTask };
	Task t19a { &TestTask };
	Task t20a { &TestTask };

	Timer tTimer;

	cntFence.target = 40;
	taskList.AddTasks( { t }, { t2 }, { t3 }, { t4 }, { t5 }, { t6 }, { t7 }, { t8 }, { t9 }, { t10 }, { t11 }, { t12 }, { t13 }, { t14 },
		{ t15 }, { t16 }, { t17 }, { t18 }, { t19 }, { t20 },
		{ ta }, { t2a }, { t3a }, { t4a }, { t5a }, { t6a }, { t7a }, { t8a }, { t9a }, { t10a }, { t11a }, { t12a }, { t13a }, { t14a },
		{ t15a }, { t16a }, { t17a }, { t18a }, { t19a }, { t20a } );

	Log::Warn( "AddTasks: %s", tTimer.FormatTime() );
	tTimer.Restart();

	cntFence.Wait();

	Log::Warn( "Complete: %s", tTimer.FormatTime() );
}