#ifndef HR_TIMER_H
#define HR_TIMER_H

#include "config.h"

#if defined(WIN32) && defined(_MSC_VER)
	#define USING_MSVC 
#endif

#if defined(_WIN32)
	#define USING_MINGW
#endif

#if defined(__APPLE__)
	#define USING_MACOSX
#endif

#if defined(__GNUC__) && !defined(USING_MACOSX) 
	#define USING_LINUX
#endif

#if defined(USING_MSVC)

	#include <windows.h>
	#pragma comment(lib, "kernel32.lib")
	typedef LARGE_INTEGER timerStruct;

    inline void GetHighResolutionTime(timerStruct *t) {
		QueryPerformanceCounter(t);
	}

	inline double ConvertTimeDifferenceToSec(timerStruct *end, timerStruct *begin) {
		timerStruct freq;

		QueryPerformanceFrequency(&freq);

		return (end->QuadPart - begin->QuadPart) / (double)freq.QuadPart;
	}

#elif defined(USING_MINGW)

	#include <windows.h>
	typedef LARGE_INTEGER timerStruct;

	inline void GetHighResolutionTime(timerStruct *t) {
		QueryPerformanceCounter(t);
	}

	inline double ConvertTimeDifferenceToSec(timerStruct *end, timerStruct *begin) {
		timerStruct freq;

		QueryPerformanceFrequency(&freq);
		
		return (end->QuadPart - begin->QuadPart) / (double)freq.QuadPart;
	}

#elif defined(USING_LINUX)  // Assume we have POSIX calls clock_gettime() 
	#include <time.h>
	typedef struct timespec timerStruct;

    inline void GetHighResolutionTime(timerStruct *t) {
#if defined(CLOCK_MONOTONIC_RAW)
		clock_gettime(CLOCK_MONOTONIC_RAW, t);
#else
		clock_gettime(CLOCK_MONOTONIC, t);
#endif
	}

	inline double ConvertTimeDifferenceToSec(timerStruct *end, timerStruct *begin) {

		timerStruct temp;

		if ((end->tv_nsec-begin->tv_nsec)<0) {
			temp.tv_sec = end->tv_sec-begin->tv_sec-1;
			temp.tv_nsec = 1000000000+end->tv_nsec-begin->tv_nsec;
		} else {
			temp.tv_sec = end->tv_sec-begin->tv_sec;
			temp.tv_nsec = end->tv_nsec-begin->tv_nsec;
		}

		return (temp.tv_sec) + (1e-9)*(temp.tv_nsec); 
	}

#elif defined(USING_MACOSX)  // Assume we're running on MacOS X

	/* this code uses calls from the CoreServices framework, so to get this to work you need to
	   add the "-framework CoreServices" parameter g++ in the linking stage. This code was adapted from:
	   http://developer.apple.com/qa/qa2004/qa1398.html
	*/

	#include <CoreServices/CoreServices.h>
	#include <mach/mach.h>
	#include <mach/mach_time.h>

	typedef uint64_t timerStruct;

	inline void GetHighResolutionTime(timerStruct *t) {
		*t = mach_absolute_time();
	}

	inline double ConvertTimeDifferenceToSec(timerStruct *end, timerStruct *begin) {
		uint64_t elapsed = *end - *begin;

		Nanoseconds elapsedNano = AbsoluteToNanoseconds(*(AbsoluteTime*)&elapsed);

		return double(*(uint64_t*)&elapsedNano) * (1e-9);
	}
#endif

#endif // end #ifndef HR_TIMER_H
