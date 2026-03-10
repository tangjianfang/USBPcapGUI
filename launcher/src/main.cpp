/*
 * USBPcapGUI Launcher
 *
 * Single-click entry point for the full USBPcapGUI stack:
 *   1. Starts bhplus-core.exe (C++ capture engine, named-pipe service)
 *   2. Starts gui-server.exe  (bundled Node.js web server)
 *   3. Opens the default browser at http://localhost:17580
 *   4. Lives in the system tray -- right-click to open or quit
 *
 * All child processes are terminated when the launcher exits.
 */

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <string>
#include <array>

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "shlwapi.lib")

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------
static constexpr UINT WM_TRAYICON    = WM_APP + 1;
static constexpr UINT ID_TRAY_OPEN   = 1001;
static constexpr UINT ID_TRAY_QUIT   = 1002;
static constexpr UINT TRAY_ICON_ID   = 1;
static constexpr int  CORE_WAIT_MS   = 600;   // time for bhplus-core to bind pipe
static constexpr int  SERVER_WAIT_MS = 1200;  // time for gui-server to bind port

static const wchar_t* APP_NAME    = L"USBPcapGUI";
static const wchar_t* WND_CLASS   = L"USBPcapGUILauncherWnd";
static const wchar_t* GUI_URL     = L"http://localhost:17580";

// ---------------------------------------------------------------------------
// Globals
// ---------------------------------------------------------------------------
static HINSTANCE g_hInst          = nullptr;
static HWND      g_hWnd           = nullptr;
static HANDLE    g_coreProcess    = INVALID_HANDLE_VALUE;
static HANDLE    g_serverProcess  = INVALID_HANDLE_VALUE;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static std::wstring GetExeDir()
{
    wchar_t path[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    PathRemoveFileSpecW(path);
    return path;
}

/// Launch a process with an optional hidden window.
/// Returns the process handle (caller owns it) or INVALID_HANDLE_VALUE on failure.
static HANDLE LaunchProcess(const std::wstring& exePath,
                             const std::wstring& args = L"",
                             bool hidden = true)
{
    std::wstring cmdLine = L"\"" + exePath + L"\"";
    if (!args.empty()) {
        cmdLine += L" ";
        cmdLine += args;
    }

    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    if (hidden) {
        si.dwFlags    = STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;
    }

    PROCESS_INFORMATION pi = {};
    BOOL ok = CreateProcessW(
        exePath.c_str(),
        cmdLine.data(),
        nullptr, nullptr,
        FALSE,
        hidden ? CREATE_NO_WINDOW : 0,
        nullptr, nullptr,
        &si, &pi);

    if (!ok) return INVALID_HANDLE_VALUE;

    CloseHandle(pi.hThread);
    return pi.hProcess;
}

static void OpenBrowserUI()
{
    ShellExecuteW(nullptr, L"open", GUI_URL, nullptr, nullptr, SW_SHOWNORMAL);
}

// ---------------------------------------------------------------------------
// System tray
// ---------------------------------------------------------------------------

static void AddTrayIcon()
{
    NOTIFYICONDATAW nid = {};
    nid.cbSize          = sizeof(nid);
    nid.hWnd            = g_hWnd;
    nid.uID             = TRAY_ICON_ID;
    nid.uFlags          = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_TRAYICON;

    // Try application icon first, fall back to system default
    nid.hIcon = static_cast<HICON>(
        LoadImageW(g_hInst, MAKEINTRESOURCEW(1), IMAGE_ICON,
                   GetSystemMetrics(SM_CXSMICON),
                   GetSystemMetrics(SM_CYSMICON), 0));
    if (!nid.hIcon)
        nid.hIcon = LoadIconW(nullptr, MAKEINTRESOURCEW(32512)); // IDI_APPLICATION

    wcscpy_s(nid.szTip, APP_NAME);
    Shell_NotifyIconW(NIM_ADD, &nid);
}

static void RemoveTrayIcon()
{
    NOTIFYICONDATAW nid = {};
    nid.cbSize = sizeof(nid);
    nid.hWnd   = g_hWnd;
    nid.uID    = TRAY_ICON_ID;
    Shell_NotifyIconW(NIM_DELETE, &nid);
}

static void ShowTrayContextMenu()
{
    POINT pt = {};
    GetCursorPos(&pt);

    HMENU hMenu = CreatePopupMenu();
    AppendMenuW(hMenu, MF_STRING, ID_TRAY_OPEN, L"Open UI (&O)");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_STRING, ID_TRAY_QUIT, L"Quit (&Q)");

    // Required so the menu disappears when clicking elsewhere
    SetForegroundWindow(g_hWnd);
    TrackPopupMenu(hMenu, TPM_BOTTOMALIGN | TPM_LEFTALIGN | TPM_RIGHTBUTTON,
                   pt.x, pt.y, 0, g_hWnd, nullptr);
    DestroyMenu(hMenu);
}

// ---------------------------------------------------------------------------
// Cleanup – terminate child processes
// ---------------------------------------------------------------------------

static void Cleanup()
{
    RemoveTrayIcon();

    if (g_serverProcess != INVALID_HANDLE_VALUE) {
        TerminateProcess(g_serverProcess, 0);
        CloseHandle(g_serverProcess);
        g_serverProcess = INVALID_HANDLE_VALUE;
    }

    if (g_coreProcess != INVALID_HANDLE_VALUE) {
        TerminateProcess(g_coreProcess, 0);
        CloseHandle(g_coreProcess);
        g_coreProcess = INVALID_HANDLE_VALUE;
    }
}

// ---------------------------------------------------------------------------
// Window procedure (message-only window)
// ---------------------------------------------------------------------------

static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {

    case WM_TRAYICON:
        switch (lParam) {
        case WM_RBUTTONUP:
            ShowTrayContextMenu();
            break;
        case WM_LBUTTONDBLCLK:
            OpenBrowserUI();
            break;
        }
        return 0;

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case ID_TRAY_OPEN:
            OpenBrowserUI();
            break;
        case ID_TRAY_QUIT:
            Cleanup();
            PostQuitMessage(0);
            break;
        }
        return 0;

    case WM_DESTROY:
        Cleanup();
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE /*hPrevInst*/, LPWSTR /*lpCmdLine*/, int /*nCmdShow*/)
{
    g_hInst = hInst;

    // Prevent multiple instances
    HANDLE hMutex = CreateMutexW(nullptr, TRUE, L"USBPcapGUILauncherMutex");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        // Another instance is running – just open the browser
        OpenBrowserUI();
        if (hMutex) CloseHandle(hMutex);
        return 0;
    }

    // Register a message-only window class for the tray
    WNDCLASSEXW wc   = {};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInst;
    wc.lpszClassName = WND_CLASS;
    RegisterClassExW(&wc);

    g_hWnd = CreateWindowExW(0, WND_CLASS, APP_NAME,
                              0, 0, 0, 0, 0,
                              HWND_MESSAGE,   // message-only -- no taskbar entry
                              nullptr, hInst, nullptr);

    const std::wstring dir = GetExeDir();

    // ------------------------------------------------------------------
    // 1. Start the C++ capture-engine service
    // ------------------------------------------------------------------
    const std::wstring corePath = dir + L"\\bhplus-core.exe";
    if (GetFileAttributesW(corePath.c_str()) == INVALID_FILE_ATTRIBUTES) {
        MessageBoxW(nullptr,
                    L"bhplus-core.exe not found.\nPlease make sure it is in the same folder as USBPcapGUI.exe.",
                    APP_NAME, MB_ICONERROR);
        return 1;
    }

    g_coreProcess = LaunchProcess(corePath);
    if (g_coreProcess == INVALID_HANDLE_VALUE) {
        MessageBoxW(nullptr,
                    L"Failed to start bhplus-core.exe.",
                    APP_NAME, MB_ICONERROR);
        return 1;
    }

    Sleep(CORE_WAIT_MS);

    // ------------------------------------------------------------------
    // 2. Start the bundled Node.js GUI server
    //    Pass --dev so the bundled server doesn't try to open the browser
    //    (we handle that below via ShellExecute).
    // ------------------------------------------------------------------
    const std::wstring serverPath = dir + L"\\gui-server.exe";
    if (GetFileAttributesW(serverPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
        MessageBoxW(nullptr,
                    L"gui-server.exe not found.\nRun scripts\\package.ps1 to bundle the GUI first.",
                    APP_NAME, MB_ICONERROR);
        Cleanup();
        return 1;
    }

    g_serverProcess = LaunchProcess(serverPath, L"--dev");
    if (g_serverProcess == INVALID_HANDLE_VALUE) {
        MessageBoxW(nullptr,
                    L"Failed to start gui-server.exe.",
                    APP_NAME, MB_ICONERROR);
        Cleanup();
        return 1;
    }

    Sleep(SERVER_WAIT_MS);

    // ------------------------------------------------------------------
    // 3. Open the browser
    // ------------------------------------------------------------------
    OpenBrowserUI();

    // ------------------------------------------------------------------
    // 4. System tray icon -- user can "打开界面" or "退出"
    // ------------------------------------------------------------------
    AddTrayIcon();

    // ------------------------------------------------------------------
    // 5. Message loop
    // ------------------------------------------------------------------
    MSG msg = {};
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (hMutex) CloseHandle(hMutex);
    return 0;
}
