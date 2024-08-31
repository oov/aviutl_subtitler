#include <ovnum.h>
#include <windows.h>

void __declspec(dllexport) __stdcall SendCtrlBreakW(HWND hwnd, HINSTANCE hinst, LPCWSTR cmdline, int cmdshow);
void __declspec(dllexport) __stdcall SendCtrlBreakW(HWND hwnd, HINSTANCE hinst, LPCWSTR cmdline, int cmdshow) {
  (void)hwnd;
  (void)hinst;
  (void)cmdshow;
  DWORD pid = 0;
  {
    int64_t v;
    if (!ov_atoi_wchar(cmdline, &v, false)) {
      OutputDebugStringW(L"Unable to parse PID");
      return;
    }
    pid = (DWORD)v;
  }
  AttachConsole(pid);
  if (!GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, pid)) {
    OutputDebugStringW(L"Unable to send Ctrl+Break");
  }
  FreeConsole();
}
