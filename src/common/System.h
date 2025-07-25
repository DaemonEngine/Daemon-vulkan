/*
===========================================================================
Daemon BSD Source Code
Copyright (c) 2013-2016, Daemon Developers
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * Neither the name of the Daemon developers nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL DAEMON DEVELOPERS BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
===========================================================================
*/

#ifndef COMMON_SYSTEM_H_
#define COMMON_SYSTEM_H_

#include <chrono>
#include <stdexcept>

#include "StackTrace.h"
#include "Compiler.h"
#include "String.h"

// Low-level system functions
namespace Sys {

#if defined(BUILD_ENGINE) && defined(_WIN32)
bool isRunningOnWine();
const char* getWineHostSystem();
#endif

int SetEnv( const char* name, const char* value );
int UnsetEnv( const char* name );

// The Windows implementation of steady_clock is really bad, use our own
#ifdef _WIN32
class SteadyClock {
public:
	using duration = std::chrono::nanoseconds;
	using rep = duration::rep;
	using period = duration::period;
	using time_point = std::chrono::time_point<SteadyClock>;
	static CONSTEXPR bool is_steady = true;

	static time_point now() NOEXCEPT;
};
#else
using SteadyClock = std::chrono::steady_clock;
#endif
using RealClock = std::chrono::system_clock;

// High precision sleep until the specified time point. Use this instead of
// std::this_thread::sleep_until. The function returns the time which should be
// used as a base to calculate the next frame's time. This will normally be the
// requested time, unless the target time is already past, in which case the
// current time is returned.
SteadyClock::time_point SleepUntil(SteadyClock::time_point time);
void SleepFor(SteadyClock::duration time);

// Returns approximately the number of milliseconds the engine has been running.
// Results *within a single module* (engine/cgame/sgame) are monotonic.
int Milliseconds();

// For a DLL this means the thread starting from the VM's entry point, not the real main thread
bool OnMainThread();

// Exit with a fatal error. Only critical subsystems are shut down cleanly, and
// an error message is displayed to the user.
NORETURN void Error(Str::StringRef errorMessage);

// Throw a DropErr with the given message, which normally will drop to the main menu
class DropErr {
public:
	DropErr(bool error, std::string message)
		: error(error), message(std::move(message)) {}
	const std::string& what() const {
		return message;
	}
	bool is_error() const {
		return error;
	}
private:
	bool error;
	std::string message;
};
NORETURN void Drop(Str::StringRef errorMessage);

// Variadic wrappers for Error and Drop
template<typename ... Args> NORETURN void Error(Str::StringRef format, Args&& ... args)
{
	PrintStackTrace();
	Error(Str::Format(format, std::forward<Args>(args)...));
}
template<typename ... Args> NORETURN void Drop(Str::StringRef format, Args&& ... args)
{
	PrintStackTrace();
	Drop(Str::Format(format, std::forward<Args>(args)...));
}

#ifdef _WIN32
// strerror() equivalent for Win32 API error values, as returned by GetLastError
std::string Win32StrError(uint32_t error);
#endif

// Initialize crash handling for the current process
void SetupCrashHandler();

// Operating system handle type
#ifdef _WIN32
// HANDLE is defined as void* in windows.h, but we don't want to include that
// everywhere. Windows also has 2 different invalid handle values: 0 and -1
using OSHandle = void*;
const OSHandle INVALID_HANDLE = reinterpret_cast<void*>(-1);
inline bool IsValidHandle(OSHandle handle)
{
	return handle != nullptr && handle != INVALID_HANDLE;
}
#else
using OSHandle = int;
const OSHandle INVALID_HANDLE = -1;
inline bool IsValidHandle(OSHandle handle)
{
	return handle != -1;
}
#endif

#ifndef __native_client__
// Class representing a loadable .dll/.so
class DynamicLib {
public:
	DynamicLib()
		: handle(nullptr) {}

	// DynamicLibs are noncopyable but movable
	DynamicLib(const DynamicLib&) = delete;
	DynamicLib& operator=(const DynamicLib&) = delete;
	DynamicLib(DynamicLib&& other)
		: handle(other.handle)
	{
		other.handle = nullptr;
	}
	DynamicLib& operator=(DynamicLib&& other)
	{
		std::swap(handle, other.handle);
		return *this;
	}

	// Check if a DynamicLib is open
	explicit operator bool() const
	{
		return handle != nullptr;
	}

	// Close a DynamicLib
	void Close();
	~DynamicLib()
	{
		Close();
	}

	// Load a DynamicLib, returns an empty DynamicLib on error
	static DynamicLib Open(Str::StringRef filename, std::string& errorString);

	// Load a symbol from the DynamicLib
	template<typename T> T* LoadSym(Str::StringRef sym, std::string& errorString)
	{
		return reinterpret_cast<T*>(InternalLoadSym(sym, errorString));
	}

private:
	intptr_t InternalLoadSym(Str::StringRef sym, std::string& errorString);

	// OS-specific handle
	void* handle;
};
#endif // __native_client__

NORETURN void OSExit(int exitCode);

bool IsProcessTerminating();

// Generate cryptographically-secure random bytes. Don't use std::random_device
// because it is not implemented correctly on some platforms.
void GenRandomBytes(void* dest, size_t size);

bool IsDebuggerAttached();

bool PedanticShutdown();

} // namespace Sys

#endif // COMMON_SYSTEM_H_
