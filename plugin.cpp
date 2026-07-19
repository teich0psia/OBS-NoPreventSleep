#include <obs-module.h>
#include <util/base.h>

#include <Windows.h>
#include <stdint.h>

using api_func = void (*)();
using api_func_bool = bool (*)();
using api_func_log = void (*)(int log_level, const char *format, ...);

OBS_DECLARE_MODULE()
OBS_MODULE_AUTHOR("Meachamp, Teich0psia")

namespace {

constexpr wchar_t kWindowClassName[] = L"OBS NoPreventSleep power state monitor";
constexpr UINT_PTR kReplayRestartTimer = 1;
constexpr UINT kReplayRestartIntervalMs = 250;
constexpr unsigned int kReplayRestartMaxAttempts = 40;
constexpr uint8_t kReturnOpcode = 0xC3;

api_func start_replay = nullptr;
api_func stop_replay = nullptr;
api_func_bool replay_active = nullptr;
api_func_log obslog = nullptr;

HANDLE monitor_thread = nullptr;
HANDLE monitor_ready_event = nullptr;
DWORD monitor_thread_id = 0;
HWND monitor_window = nullptr;
bool monitor_started = false;

uint8_t *sleep_inhibit_function = nullptr;
uint8_t original_sleep_inhibit_byte = 0;
bool owns_sleep_inhibit_patch = false;

bool replay_active_before_suspend = false;
bool replay_restart_pending = false;
unsigned int replay_restart_attempts = 0;

void log_windows_error(const char *operation)
{
	const DWORD error = GetLastError();
	if (obslog) {
		obslog(LOG_ERROR, "[NoPreventSleep] %s failed (Windows error %lu).", operation, error);
	} else {
		OutputDebugStringA("[NoPreventSleep] A Windows API call failed during initialization.\n");
	}
}

template<typename T> bool resolve_api(HMODULE module, const char *name, T &function)
{
	function = reinterpret_cast<T>(GetProcAddress(module, name));
	if (!function && obslog)
		obslog(LOG_ERROR, "[NoPreventSleep] Required OBS API '%s' was not found.", name);
	return function != nullptr;
}

bool resolve_obs_apis()
{
	const HMODULE obs_dll = GetModuleHandleW(L"obs.dll");
	if (!obs_dll) {
		log_windows_error("GetModuleHandleW(obs.dll)");
		return false;
	}

	if (!resolve_api(obs_dll, "blog", obslog))
		return false;

	const HMODULE obs_frontend = GetModuleHandleW(L"obs-frontend-api.dll");
	if (!obs_frontend) {
		log_windows_error("GetModuleHandleW(obs-frontend-api.dll)");
		return false;
	}

	return resolve_api(obs_frontend, "obs_frontend_replay_buffer_start", start_replay) &&
	       resolve_api(obs_frontend, "obs_frontend_replay_buffer_stop", stop_replay) &&
	       resolve_api(obs_frontend, "obs_frontend_replay_buffer_active", replay_active) &&
	       resolve_api(obs_dll, "os_inhibit_sleep_set_active", sleep_inhibit_function);
}

bool write_sleep_inhibit_byte(uint8_t value)
{
	DWORD old_protection = 0;
	if (!VirtualProtect(sleep_inhibit_function, 1, PAGE_EXECUTE_READWRITE, &old_protection)) {
		log_windows_error("VirtualProtect(PAGE_EXECUTE_READWRITE)");
		return false;
	}

	InterlockedExchange8(reinterpret_cast<volatile char *>(sleep_inhibit_function), static_cast<char>(value));
	const BOOL cache_flushed = FlushInstructionCache(GetCurrentProcess(), sleep_inhibit_function, 1);
	if (!cache_flushed)
		log_windows_error("FlushInstructionCache");

	DWORD ignored_protection = 0;
	const BOOL protection_restored = VirtualProtect(sleep_inhibit_function, 1, old_protection, &ignored_protection);
	if (!protection_restored)
		log_windows_error("VirtualProtect(restore)");

	return cache_flushed && protection_restored;
}

bool install_sleep_inhibit_patch()
{
	original_sleep_inhibit_byte = *sleep_inhibit_function;
	if (original_sleep_inhibit_byte == kReturnOpcode) {
		obslog(LOG_WARNING,
		       "[NoPreventSleep] OBS sleep inhibition was already patched; this module will not restore it.");
		return true;
	}

	if (!write_sleep_inhibit_byte(kReturnOpcode)) {
		write_sleep_inhibit_byte(original_sleep_inhibit_byte);
		return false;
	}

	owns_sleep_inhibit_patch = true;
	return true;
}

void uninstall_sleep_inhibit_patch()
{
	if (!owns_sleep_inhibit_patch || !sleep_inhibit_function)
		return;

	if (*sleep_inhibit_function != kReturnOpcode) {
		obslog(LOG_WARNING,
		       "[NoPreventSleep] OBS sleep inhibition changed after load; refusing to overwrite another patch.");
	} else if (!write_sleep_inhibit_byte(original_sleep_inhibit_byte)) {
		obslog(LOG_ERROR, "[NoPreventSleep] Failed to restore OBS sleep inhibition.");
	}

	owns_sleep_inhibit_patch = false;
}

void cancel_replay_restart(HWND hwnd)
{
	KillTimer(hwnd, kReplayRestartTimer);
	replay_restart_pending = false;
	replay_restart_attempts = 0;
}

void attempt_replay_restart(HWND hwnd)
{
	if (!replay_restart_pending)
		return;

	if (!replay_active()) {
		cancel_replay_restart(hwnd);
		obslog(LOG_INFO, "[NoPreventSleep] Restarting the replay buffer after resume.");
		start_replay();
		return;
	}

	if (++replay_restart_attempts >= kReplayRestartMaxAttempts) {
		obslog(LOG_ERROR,
		       "[NoPreventSleep] Replay buffer did not stop within 10 seconds; restart was cancelled.");
		cancel_replay_restart(hwnd);
	}
}

LRESULT CALLBACK window_proc(HWND hwnd, UINT message, WPARAM w_param, LPARAM l_param)
{
	if (message == WM_POWERBROADCAST) {
		if (w_param == PBT_APMSUSPEND) {
			cancel_replay_restart(hwnd);
			replay_active_before_suspend = replay_active();
			obslog(LOG_INFO, "[NoPreventSleep] Suspend notification received (replay buffer: %s).",
			       replay_active_before_suspend ? "active" : "inactive");

			if (replay_active_before_suspend)
				stop_replay();
			return TRUE;
		}

		if (w_param == PBT_APMRESUMEAUTOMATIC) {
			obslog(LOG_INFO, "[NoPreventSleep] Resume notification received.");
			if (!replay_active_before_suspend)
				return TRUE;

			replay_active_before_suspend = false;
			replay_restart_pending = true;
			replay_restart_attempts = 0;

			if (replay_active())
				stop_replay();

			attempt_replay_restart(hwnd);
			if (replay_restart_pending && !SetTimer(hwnd, kReplayRestartTimer, kReplayRestartIntervalMs, nullptr)) {
				log_windows_error("SetTimer");
				cancel_replay_restart(hwnd);
			}
			return TRUE;
		}
	}

	if (message == WM_TIMER && w_param == kReplayRestartTimer) {
		attempt_replay_restart(hwnd);
		return 0;
	}

	return DefWindowProcW(hwnd, message, w_param, l_param);
}

DWORD WINAPI monitor_loop(LPVOID)
{
	HINSTANCE plugin_instance = nullptr;
	if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
	                        kWindowClassName, &plugin_instance)) {
		log_windows_error("GetModuleHandleExW(plugin)");
		SetEvent(monitor_ready_event);
		return 1;
	}

	WNDCLASSEXW window_class = {};
	window_class.cbSize = sizeof(window_class);
	window_class.lpfnWndProc = window_proc;
	window_class.hInstance = plugin_instance;
	window_class.lpszClassName = kWindowClassName;

	if (!RegisterClassExW(&window_class)) {
		log_windows_error("RegisterClassExW");
		SetEvent(monitor_ready_event);
		return 1;
	}

	monitor_window = CreateWindowExW(0, kWindowClassName, kWindowClassName, 0, 0, 0, 0, 0, nullptr, nullptr,
	                               plugin_instance, nullptr);
	if (!monitor_window) {
		log_windows_error("CreateWindowExW");
		UnregisterClassW(kWindowClassName, plugin_instance);
		SetEvent(monitor_ready_event);
		return 1;
	}

	monitor_started = true;
	SetEvent(monitor_ready_event);
	obslog(LOG_INFO, "[NoPreventSleep] Power-state monitor started.");

	MSG message = {};
	int get_message_result = 0;
	while ((get_message_result = GetMessageW(&message, nullptr, 0, 0)) > 0) {
		TranslateMessage(&message);
		DispatchMessageW(&message);
	}

	if (get_message_result == -1)
		log_windows_error("GetMessageW");

	cancel_replay_restart(monitor_window);
	DestroyWindow(monitor_window);
	monitor_window = nullptr;
	UnregisterClassW(kWindowClassName, plugin_instance);
	return get_message_result == -1 ? 1 : 0;
}

bool start_monitor()
{
	monitor_ready_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
	if (!monitor_ready_event) {
		log_windows_error("CreateEventW");
		return false;
	}

	monitor_thread = CreateThread(nullptr, 0, monitor_loop, nullptr, 0, &monitor_thread_id);
	if (!monitor_thread) {
		log_windows_error("CreateThread");
		CloseHandle(monitor_ready_event);
		monitor_ready_event = nullptr;
		return false;
	}

	const DWORD wait_result = WaitForSingleObject(monitor_ready_event, 5000);
	if (wait_result != WAIT_OBJECT_0 || !monitor_started) {
		if (wait_result == WAIT_FAILED)
			log_windows_error("WaitForSingleObject(monitor ready)");
		else if (wait_result == WAIT_TIMEOUT)
			obslog(LOG_ERROR, "[NoPreventSleep] Power-state monitor initialization timed out.");

		PostThreadMessageW(monitor_thread_id, WM_QUIT, 0, 0);
		WaitForSingleObject(monitor_thread, INFINITE);
		CloseHandle(monitor_thread);
		CloseHandle(monitor_ready_event);
		monitor_thread = nullptr;
		monitor_ready_event = nullptr;
		monitor_thread_id = 0;
		return false;
	}

	return true;
}

void stop_monitor()
{
	if (monitor_thread) {
		if (!PostThreadMessageW(monitor_thread_id, WM_QUIT, 0, 0))
			log_windows_error("PostThreadMessageW(WM_QUIT)");
		WaitForSingleObject(monitor_thread, INFINITE);
		CloseHandle(monitor_thread);
		monitor_thread = nullptr;
	}

	if (monitor_ready_event) {
		CloseHandle(monitor_ready_event);
		monitor_ready_event = nullptr;
	}

	monitor_thread_id = 0;
	monitor_started = false;
}

} // namespace

bool obs_module_load()
{
	if (!resolve_obs_apis())
		return false;

	if (!start_monitor())
		return false;

	if (!install_sleep_inhibit_patch()) {
		stop_monitor();
		return false;
	}

	obslog(LOG_INFO, "[NoPreventSleep] OBS sleep inhibition disabled.");
	return true;
}

void obs_module_unload()
{
	stop_monitor();
	uninstall_sleep_inhibit_patch();

	if (obslog)
		obslog(LOG_INFO, "[NoPreventSleep] Module unloaded and OBS sleep inhibition restored.");
}

const char *obs_module_name()
{
	return "OBS NoPreventSleep";
}

const char *obs_module_description()
{
	return "Allows Windows to sleep while the OBS replay buffer is active and restarts the buffer after resume.";
}
