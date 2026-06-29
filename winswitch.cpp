#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <vector>
#include <string>
#include <algorithm>
#include <cwctype>
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "shell32.lib")

#ifndef DWMWA_CLOAKED
#define DWMWA_CLOAKED 14
#endif

#define HOTKEY_ID 1
#define TRAY_UID 1
#define WM_TRAYICON (WM_APP + 1)
#define IDM_OPEN 1001
#define IDM_EXIT 1002

struct WindowEntry {
    HWND hwnd;
    std::wstring title;
    std::wstring lower;
};

static std::vector<WindowEntry> g_windows;
static std::vector<int> g_filtered;
static HWND g_main = nullptr, g_edit = nullptr, g_list = nullptr;
static WNDPROC g_editProc = nullptr;
static HFONT g_font = nullptr;
static NOTIFYICONDATAW g_nid = {};

static bool IsAltTabWindow(HWND hwnd) {
    if (!IsWindowVisible(hwnd)) return false;

    HWND root = GetAncestor(hwnd, GA_ROOTOWNER);
    HWND lastPopup = root, walk = nullptr;
    do {
        walk = lastPopup;
        lastPopup = GetLastActivePopup(walk);
        if (IsWindowVisible(lastPopup)) break;
    } while (lastPopup != walk);
    if (lastPopup != hwnd) return false;

    if (GetWindowTextLengthW(hwnd) == 0) return false;

    LONG_PTR ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    if (ex & WS_EX_TOOLWINDOW) return false;

    int cloaked = 0;
    DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
    if (cloaked) return false;

    return true;
}

static BOOL CALLBACK EnumProc(HWND hwnd, LPARAM) {
    if (hwnd != g_main && IsAltTabWindow(hwnd)) {
        wchar_t buf[512];
        GetWindowTextW(hwnd, buf, 512);
        std::wstring title = buf;
        std::wstring lower = title;
        for (auto& c : lower) c = (wchar_t)towlower(c);
        g_windows.push_back({hwnd, std::move(title), std::move(lower)});
    }
    return TRUE;
}

static void Refilter() {
    wchar_t buf[256];
    GetWindowTextW(g_edit, buf, 256);
    std::wstring needle = buf;
    for (auto& c : needle) c = (wchar_t)towlower(c);

    struct Scored { int index; int score; };
    std::vector<Scored> scored;

    for (int i = 0; i < (int)g_windows.size(); ++i) {
        const std::wstring& hay = g_windows[i].lower;
        int score;

        if (needle.empty()) {
            score = -(int)hay.size();
        } else {
            size_t pos = hay.find(needle);
            if (pos != std::wstring::npos) {
                score = 2000 - (int)pos - (int)hay.size();
            } else {
                size_t k = 0, first = 0, last = 0;
                for (size_t j = 0; j < hay.size() && k < needle.size(); ++j) {
                    if (hay[j] == needle[k]) {
                        if (k == 0) first = j;
                        last = j;
                        ++k;
                    }
                }
                if (k < needle.size()) continue;
                score = 500 - (int)(last - first);
            }
        }
        scored.push_back({i, score});
    }

    std::stable_sort(scored.begin(), scored.end(),
        [](const Scored& a, const Scored& b) { return a.score > b.score; });

    g_filtered.clear();
    SendMessage(g_list, LB_RESETCONTENT, 0, 0);
    for (auto& s : scored) {
        g_filtered.push_back(s.index);
        SendMessageW(g_list, LB_ADDSTRING, 0, (LPARAM)g_windows[s.index].title.c_str());
    }
    if (!g_filtered.empty()) SendMessage(g_list, LB_SETCURSEL, 0, 0);
}

static void HidePalette() {
    ShowWindow(g_main, SW_HIDE);
}

static void ForceForeground(HWND target) {
    if (IsIconic(target)) ShowWindow(target, SW_RESTORE);

    DWORD fgThread = GetWindowThreadProcessId(GetForegroundWindow(), nullptr);
    DWORD myThread = GetCurrentThreadId();
    AttachThreadInput(myThread, fgThread, TRUE);
    SetForegroundWindow(target);
    BringWindowToTop(target);
    AttachThreadInput(myThread, fgThread, FALSE);
}

static void ActivateSelected() {
    int sel = (int)SendMessage(g_list, LB_GETCURSEL, 0, 0);
    if (sel < 0 || sel >= (int)g_filtered.size()) return;
    HWND target = g_windows[g_filtered[sel]].hwnd;
    ForceForeground(target);
    HidePalette();
}

static void MoveSelection(int delta) {
    int count = (int)SendMessage(g_list, LB_GETCOUNT, 0, 0);
    if (count == 0) return;
    int cur = (int)SendMessage(g_list, LB_GETCURSEL, 0, 0);
    if (cur < 0) cur = 0;
    cur = (cur + delta + count) % count;
    SendMessage(g_list, LB_SETCURSEL, cur, 0);
}

static void ShowPalette() {
    g_windows.clear();
    EnumWindows(EnumProc, 0);

    SetWindowTextW(g_edit, L"");
    Refilter();

    RECT work;
    SystemParametersInfo(SPI_GETWORKAREA, 0, &work, 0);
    int width = 620, height = 440;
    int x = work.left + ((work.right - work.left) - width) / 2;
    int y = work.top + ((work.bottom - work.top) - height) / 4;

    SetWindowPos(g_main, HWND_TOPMOST, x, y, width, height, SWP_SHOWWINDOW);
    SetForegroundWindow(g_main);
    SetFocus(g_edit);
}

static LRESULT CALLBACK EditProc(HWND h, UINT msg, WPARAM w, LPARAM l) {
    if (msg == WM_KEYDOWN) {
        switch (w) {
            case VK_ESCAPE: HidePalette(); return 0;
            case VK_RETURN: ActivateSelected(); return 0;
            case VK_DOWN:   MoveSelection(1);  return 0;
            case VK_UP:     MoveSelection(-1); return 0;
        }
    } else if (msg == WM_CHAR) {
        if (w == VK_RETURN || w == VK_ESCAPE || w == L'\n') return 0;
    }
    return CallWindowProc(g_editProc, h, msg, w, l);
}

static void LayoutChildren(int width, int height) {
    int pad = 8, editHeight = 30;
    MoveWindow(g_edit, pad, pad, width - 2 * pad, editHeight, TRUE);
    int listTop = pad + editHeight + pad;
    MoveWindow(g_list, pad, listTop, width - 2 * pad, height - listTop - pad, TRUE);
}

static void AddTrayIcon() {
    g_nid.cbSize = sizeof(g_nid);
    g_nid.hWnd = g_main;
    g_nid.uID = TRAY_UID;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAYICON;
    // Retro race-car icon from the classic Win9x PIF manager (index 27).
    g_nid.hIcon = ExtractIconW(GetModuleHandleW(nullptr), L"pifmgr.dll", 27);
    if (!g_nid.hIcon) g_nid.hIcon = LoadIcon(nullptr, IDI_APPLICATION); // ponytail: fallback if DLL/index missing
    wcscpy_s(g_nid.szTip, L"WinSwitch — Ctrl+Alt+Space");
    Shell_NotifyIconW(NIM_ADD, &g_nid);
}

static void RemoveTrayIcon() {
    Shell_NotifyIconW(NIM_DELETE, &g_nid);
}

static void ShowTrayMenu() {
    POINT pt;
    GetCursorPos(&pt);

    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, IDM_OPEN, L"Open (Ctrl+Alt+Space)");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, IDM_EXIT, L"Exit");

    // Required so the menu dismisses when the user clicks elsewhere.
    SetForegroundWindow(g_main);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN,
                   pt.x, pt.y, 0, g_main, nullptr);
    DestroyMenu(menu);
}

static LRESULT CALLBACK WndProc(HWND h, UINT msg, WPARAM w, LPARAM l) {
    switch (msg) {
        case WM_HOTKEY:
            if (w == HOTKEY_ID) ShowPalette();
            return 0;

        case WM_TRAYICON:
            if (LOWORD(l) == WM_LBUTTONUP || LOWORD(l) == WM_LBUTTONDBLCLK) {
                ShowPalette();
            } else if (LOWORD(l) == WM_RBUTTONUP || LOWORD(l) == WM_CONTEXTMENU) {
                ShowTrayMenu();
            }
            return 0;

        case WM_COMMAND:
            if (l == 0) {
                if (LOWORD(w) == IDM_OPEN) ShowPalette();
                else if (LOWORD(w) == IDM_EXIT) DestroyWindow(g_main);
                return 0;
            }
            if ((HWND)l == g_edit && HIWORD(w) == EN_CHANGE) Refilter();
            if ((HWND)l == g_list && HIWORD(w) == LBN_DBLCLK) ActivateSelected();
            return 0;

        case WM_ACTIVATE:
            if (LOWORD(w) == WA_INACTIVE) HidePalette();
            return 0;

        case WM_SIZE:
            LayoutChildren(LOWORD(l), HIWORD(l));
            return 0;

        case WM_DESTROY:
            RemoveTrayIcon();
            UnregisterHotKey(g_main, HOTKEY_ID);
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProc(h, msg, w, l);
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, LPWSTR, int) {
    SetProcessDPIAware();

    WNDCLASSW wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.lpszClassName = L"WinSwitchPalette";
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    RegisterClassW(&wc);

    g_main = CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
        L"WinSwitchPalette", L"",
        WS_POPUP | WS_BORDER,
        0, 0, 620, 440,
        nullptr, nullptr, inst, nullptr);

    g_font = CreateFontW(20, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");

    g_edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
        0, 0, 0, 0, g_main, nullptr, inst, nullptr);

    g_list = CreateWindowExW(0, L"LISTBOX", L"",
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT,
        0, 0, 0, 0, g_main, nullptr, inst, nullptr);

    SendMessage(g_edit, WM_SETFONT, (WPARAM)g_font, TRUE);
    SendMessage(g_list, WM_SETFONT, (WPARAM)g_font, TRUE);

    g_editProc = (WNDPROC)SetWindowLongPtr(g_edit, GWLP_WNDPROC, (LONG_PTR)EditProc);

    RECT client;
    GetClientRect(g_main, &client);
    LayoutChildren(client.right, client.bottom);

    if (!RegisterHotKey(g_main, HOTKEY_ID, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, VK_SPACE)) {
        MessageBoxW(g_main, L"Could not register Ctrl+Alt+Space hotkey.", L"WinSwitch", MB_OK);
    }

    AddTrayIcon();

    MSG m;
    while (GetMessage(&m, nullptr, 0, 0)) {
        TranslateMessage(&m);
        DispatchMessage(&m);
    }
    return 0;
}
