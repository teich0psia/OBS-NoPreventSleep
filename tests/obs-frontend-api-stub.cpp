#include <atomic>

namespace {
std::atomic<bool> active = false;
std::atomic<int> start_call_count = 0;
std::atomic<int> stop_call_count = 0;
}

extern "C" __declspec(dllexport) void obs_frontend_replay_buffer_start()
{
	start_call_count.fetch_add(1);
	active.store(true);
}

extern "C" __declspec(dllexport) void obs_frontend_replay_buffer_stop()
{
	stop_call_count.fetch_add(1);
	active.store(false);
}

extern "C" __declspec(dllexport) bool obs_frontend_replay_buffer_active()
{
	return active.load();
}

extern "C" __declspec(dllexport) void test_set_replay_active(bool value)
{
	active.store(value);
}

extern "C" __declspec(dllexport) void test_reset_replay_call_counts()
{
	start_call_count.store(0);
	stop_call_count.store(0);
}

extern "C" __declspec(dllexport) int test_get_start_call_count()
{
	return start_call_count.load();
}

extern "C" __declspec(dllexport) int test_get_stop_call_count()
{
	return stop_call_count.load();
}
