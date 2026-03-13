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
#include "log_init.h"

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
/// The process is created in its own process group so we can signal it cleanly.
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

    // CREATE_NEW_PROCESS_GROUP: lets us send CTRL_BREAK_EVENT to this
    // process specifically (its group ID == its PID).
    DWORD flags = CREATE_NEW_PROCESS_GROUP;
    if (hidden) flags |= CREATE_NO_WINDOW;

    PROCESS_INFORMATION pi = {};
    BOOL ok = CreateProcessW(
        exePath.c_str(),
        cmdLine.data(),
        nullptr, nullptr,
        FALSE,
        flags,
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

    // Required on Windows 10/11 for reliable tray icon visibility
    NOTIFYICONDATAW nidVer = {};
    nidVer.cbSize  = sizeof(nidVer);
    nidVer.hWnd    = g_hWnd;
    nidVer.uID     = TRAY_ICON_ID;
    nidVer.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &nidVer);
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

/// Gracefully terminate a child process:
///   1. Send CTRL_BREAK_EVENT (triggers Node.js SIGINT handler, flushes logs)
///   2. Wait up to `waitMs` for the process to exit on its own
///   3. Hard-kill with TerminateProcess if it's still alive
static void GracefulTerminate(HANDLE hProcess, DWORD waitMs = 2000)
{
    if (hProcess == INVALID_HANDLE_VALUE) return;

    // CTRL_BREAK_EVENT target is the process-group ID.
    // Because we created with CREATE_NEW_PROCESS_GROUP, the group ID == PID.
    DWORD pid = GetProcessId(hProcess);
    if (pid) {
        GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, pid);
        if (WaitForSingleObject(hProcess, waitMs) == WAIT_OBJECT_0) {
            return;  // process exited cleanly
        }
    }
    // Fallback: hard kill
    TerminateProcess(hProcess, 0);
    WaitForSingleObject(hProcess, 1000);
}

// ---------------------------------------------------------------------------
// Cleanup – terminate child processes
// ---------------------------------------------------------------------------

static void Cleanup()
{
    RemoveTrayIcon();

    if (g_serverProcess != INVALID_HANDLE_VALUE) {
        GracefulTerminate(g_serverProcess);
        CloseHandle(g_serverProcess);
        g_serverProcess = INVALID_HANDLE_VALUE;
    }

    if (g_coreProcess != INVALID_HANDLE_VALUE) {
        GracefulTerminate(g_coreProcess);
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
        // With NOTIFYICON_VERSION_4, LOWORD(lParam) is the mouse event,
        // HIWORD(lParam) is the icon ID.
        switch (LOWORD(lParam)) {
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
    InitLogger("USBPcapGUI", false);  // GUI app — file only, no console

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
    //    Prefer gui-server.exe (pkg-bundled); fall back to node.exe + gui\server.js
    // ------------------------------------------------------------------
    const std::wstring serverPath     = dir + L"\\gui-server.exe";
    const std::wstring nodePath       = dir + L"\\node.exe";
    const std::wstring nodeServerPath = dir + L"\\gui\\server.js";

    bool useNodeFallback = false;
    if (GetFileAttributesW(serverPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
        // gui-server.exe not found – try node.exe + gui\server.js
        if (GetFileAttributesW(nodePath.c_str()) == INVALID_FILE_ATTRIBUTES ||
            GetFileAttributesW(nodeServerPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
            MessageBoxW(nullptr,
                        L"GUI server not found.\n\n"
                        L"Expected either:\n"
                        L"  gui-server.exe  (bundled)\n"
                        L"  node.exe + gui\\server.js  (portable Node.js)\n\n"
                        L"Run scripts\\package.ps1 to package the GUI.",
                        APP_NAME, MB_ICONERROR);
            Cleanup();
            return 1;
        }
        useNodeFallback = true;
    }

    if (useNodeFallback) {
        // Launch: node.exe "<dir>\gui\server.js" --dev --no-spawn-core
        // --no-spawn-core tells the server not to re-launch bhplus-core (we already did it above)
        std::wstring args = L"\"" + nodeServerPath + L"\" --dev --no-spawn-core";
        // CreateProcess needs the executable to be node.exe, not server.js
        std::wstring cmdLine = L"\"" + nodePath + L"\" " + args;
        STARTUPINFOW si = {}; si.cb = sizeof(si);
        si.dwFlags = STARTF_USESHOWWINDOW; si.wShowWindow = SW_HIDE;
        PROCESS_INFORMATION pi = {};
        if (!CreateProcessW(nodePath.c_str(), cmdLine.data(),
                            nullptr, nullptr, FALSE,
                            CREATE_NO_WINDOW | CREATE_NEW_PROCESS_GROUP,
                            nullptr, nullptr, &si, &pi)) {
            MessageBoxW(nullptr, L"Failed to start node.exe.", APP_NAME, MB_ICONERROR);
            Cleanup();
            return 1;
        }
        CloseHandle(pi.hThread);
        g_serverProcess = pi.hProcess;
    } else {
        g_serverProcess = LaunchProcess(serverPath, L"--dev --no-spawn-core");
        if (g_serverProcess == INVALID_HANDLE_VALUE) {
            MessageBoxW(nullptr, L"Failed to start gui-server.exe.", APP_NAME, MB_ICONERROR);
            Cleanup();
            return 1;
        }
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
