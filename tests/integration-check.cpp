#include <Windows.h>

#include <cstdio>
#include <filesystem>

namespace {

constexpr wchar_t kWindowClassName[] = L"OBS NoPreventSleep power state monitor";

template<typename T> T get_export(HMODULE module, const char *name)
{
	auto function = reinterpret_cast<T>(GetProcAddress(module, name));
	if (!function)
		std::printf("Missing export: %s\n", name);
	return function;
}

bool expect(bool condition, const char *message)
{
	if (!condition)
		std::printf("FAILED: %s\n", message);
	return condition;
}

} // namespace

int wmain(int argc, wchar_t **argv)
{
	if (argc != 2) {
		std::printf("Usage: integration-check <plugin.dll>\n");
		return 2;
	}

	wchar_t executable_path[MAX_PATH] = {};
	if (!GetModuleFileNameW(nullptr, executable_path, MAX_PATH))
		return 1;
	const std::filesystem::path test_directory = std::filesystem::path(executable_path).parent_path();

	const HMODULE obs_module = LoadLibraryW((test_directory / L"obs.dll").c_str());
	const HMODULE frontend_module = LoadLibraryW((test_directory / L"obs-frontend-api.dll").c_str());
	const HMODULE plugin_module = LoadLibraryW(argv[1]);
	if (!expect(obs_module && frontend_module && plugin_module, "test modules and plugin must load"))
		return 1;

	using void_func = void (*)();
	using bool_func = bool (*)();
	using bool_arg_func = void (*)(bool);
	using int_func = int (*)();

	const auto module_load = get_export<bool_func>(plugin_module, "obs_module_load");
	const auto module_unload = get_export<void_func>(plugin_module, "obs_module_unload");
	const auto inhibit_sleep = get_export<bool_arg_func>(obs_module, "os_inhibit_sleep_set_active");
	const auto get_inhibit_calls = get_export<int_func>(obs_module, "test_get_inhibit_call_count");
	const auto set_replay_active = get_export<bool_arg_func>(frontend_module, "test_set_replay_active");
	const auto reset_replay_calls = get_export<void_func>(frontend_module, "test_reset_replay_call_counts");
	const auto get_start_calls = get_export<int_func>(frontend_module, "test_get_start_call_count");
	const auto get_stop_calls = get_export<int_func>(frontend_module, "test_get_stop_call_count");
	if (!module_load || !module_unload || !inhibit_sleep || !get_inhibit_calls || !set_replay_active ||
	    !reset_replay_calls || !get_start_calls || !get_stop_calls)
		return 1;

	inhibit_sleep(true);
	if (!expect(get_inhibit_calls() == 1, "sleep-inhibition stub must work before module load"))
		return 1;
	if (!expect(module_load(), "obs_module_load must succeed"))
		return 1;

	const HWND monitor = FindWindowW(kWindowClassName, kWindowClassName);
	if (!expect(monitor != nullptr, "power-state monitor window must exist after load"))
		return 1;

	inhibit_sleep(true);
	if (!expect(get_inhibit_calls() == 1, "sleep-inhibition call must be disabled while plugin is loaded"))
		return 1;

	set_replay_active(false);
	reset_replay_calls();
	SendMessageW(monitor, WM_POWERBROADCAST, PBT_APMSUSPEND, 0);
	SendMessageW(monitor, WM_POWERBROADCAST, PBT_APMRESUMEAUTOMATIC, 0);
	if (!expect(get_start_calls() == 0 && get_stop_calls() == 0,
	            "an inactive replay buffer must remain inactive across suspend/resume"))
		return 1;

	set_replay_active(true);
	reset_replay_calls();
	SendMessageW(monitor, WM_POWERBROADCAST, PBT_APMSUSPEND, 0);
	if (!expect(get_stop_calls() == 1, "an active replay buffer must stop on suspend"))
		return 1;
	SendMessageW(monitor, WM_POWERBROADCAST, PBT_APMRESUMEAUTOMATIC, 0);
	if (!expect(get_start_calls() == 1, "the previously active replay buffer must restart on resume"))
		return 1;

	module_unload();
	if (!expect(FindWindowW(kWindowClassName, kWindowClassName) == nullptr,
	            "power-state monitor window must be destroyed during unload"))
		return 1;

	inhibit_sleep(true);
	if (!expect(get_inhibit_calls() == 2, "sleep-inhibition function must be restored during unload"))
		return 1;

	FreeLibrary(plugin_module);
	FreeLibrary(frontend_module);
	FreeLibrary(obs_module);
	std::printf("Integration checks passed.\n");
	return 0;
}
