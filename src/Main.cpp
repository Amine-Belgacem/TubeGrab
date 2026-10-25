#include "App.h"
#include <shellapi.h>
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_STANDARD_CLASSES | ICC_LISTVIEW_CLASSES | ICC_TAB_CLASSES | ICC_PROGRESS_CLASS | ICC_BAR_CLASSES};
    InitCommonControlsEx(&controls);
    int result = 1;
    try {
        if (FAILED(com)) throw std::runtime_error("Windows COM initialization failed.");
        auto lockPath = cf::DataDirectory() / L"instance.lock";
        // An exclusive file handle also serializes instances in different Windows sessions.
        cf::Handle instanceLock(CreateFileW(lockPath.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
        if (instanceLock.h == INVALID_HANDLE_VALUE) {
            DWORD error = GetLastError();
            if (error != ERROR_SHARING_VIOLATION) throw std::runtime_error("Cannot lock local data: " + cf::Utf8(cf::WinError(error)));
            HWND existing = FindWindowW(L"TubeGrabWindow", nullptr);
            if (!existing) existing = FindWindowW(L"ClearFetchWindow", nullptr);
            if (existing) { ShowWindow(existing, SW_RESTORE); SetForegroundWindow(existing); }
            else MessageBoxW(nullptr, L"TubeGrab is already running in another Windows session.", L"TubeGrab", MB_OK);
            result = 0;
        } else { cf::App app; result = app.Run(instance, show); }
    }
    catch (const std::exception& e) { MessageBoxW(nullptr, cf::Wide(e.what()).c_str(), L"TubeGrab could not start", MB_OK | MB_ICONERROR); }
    if (SUCCEEDED(com)) CoUninitialize();
    return result;
}
