#include <Windows.h>

#include <atomic>
#include <cstdarg>
#include <cstdio>

namespace {
std::atomic<int> inhibit_call_count = 0;
}

extern "C" __declspec(dllexport) void blog(int, const char *format, ...)
{
	va_list args;
	va_start(args, format);
	std::vprintf(format, args);
	std::printf("\n");
	va_end(args);
}

extern "C" __declspec(dllexport) __declspec(noinline) void os_inhibit_sleep_set_active(bool)
{
	inhibit_call_count.fetch_add(1);
}

extern "C" __declspec(dllexport) int test_get_inhibit_call_count()
{
	return inhibit_call_count.load();
}
