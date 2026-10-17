#define WIN32_LEAN_AND_MEAN
#define UNICODE
#define _UNICODE
#define WINVER 0x0601
#define _WIN32_WINNT 0x0601
#include <Windows.h>
#include <shellapi.h>

#include "battery-warn.rc.h"
#include "toast.h"

/* ---------------- constants ---------------- */

static constexpr LPCWSTR APP_ID    = L"BatteryWarn";
static constexpr LPCWSTR APP_TITLE = L"Battery Warn";
static constexpr LPCWSTR REG_PATH  = L"Software\\BatteryWarn";
static constexpr LPCWSTR REG_VALUE = L"Threshold";

static constexpr DWORD   DEFAULT_THRESHOLD = 5;
static constexpr UINT    POLL_MS           = 30000;
static constexpr UINT    WM_TRAY           = WM_APP + 1;
static constexpr UINT    TIMER_ID          = 1;
static constexpr UINT    TIMER_ID_COUNTDOWN = 2;

static constexpr int     COUNTDOWN_SECONDS  = 60;

static constexpr UINT MENU_INFO      = 1000;
static constexpr UINT MENU_PRESET_5  = 1005;
static constexpr UINT MENU_PRESET_10 = 1010;
static constexpr UINT MENU_PRESET_15 = 1015;
static constexpr UINT MENU_PRESET_20 = 1020;
static constexpr UINT MENU_PRESET_25 = 1025;
static constexpr UINT MENU_PRESET_30 = 1030;
static constexpr UINT MENU_CUSTOM    = 1099;
static constexpr UINT MENU_TEST      = 1100;
static constexpr UINT MENU_EXIT      = 1101;

/* ---------------- library + OS helpers ---------------- */

struct LibraryLoader
{
private:
    HMODULE module;

public:
    LibraryLoader(const LibraryLoader &)            = delete;
    LibraryLoader &operator=(const LibraryLoader &) = delete;

    LibraryLoader(LPCWSTR name) : module(GetModuleHandleW(name))
    {
        if (!module) module = LoadLibraryW(name);
    }

    template <typename T>
    T getProcedure(const char *name) const
    {
        return reinterpret_cast<T>(reinterpret_cast<void *>(GetProcAddress(module, name)));
    }
};

static void enable_hidpi()
{
    LibraryLoader user32(L"user32.dll");
    if (auto pSetProcessDpiAwarenessContext = user32.getProcedure<BOOL(WINAPI *)(DPI_AWARENESS_CONTEXT)>(
            "SetProcessDpiAwarenessContext"))
        pSetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    else if (auto pSetProcessDPIAware = user32.getProcedure<BOOL(WINAPI *)()>("SetProcessDPIAware"))
        pSetProcessDPIAware();
}

[[nodiscard]] static DWORD get_build_number()
{
    LibraryLoader ntdll(L"ntdll.dll");
    if (auto pRtlGetVersion = ntdll.getProcedure<LONG(WINAPI *)(PRTL_OSVERSIONINFOW)>("RtlGetVersion"))
    {
        RTL_OSVERSIONINFOW rovi;
        rovi.dwOSVersionInfoSize = sizeof rovi;
        if (pRtlGetVersion(&rovi) == 0)
            return rovi.dwBuildNumber;
    }
    return 0;
}

static void enable_dark_mode_support()
{
    DWORD build = get_build_number();
    if (build < 17763)
        return;

    LibraryLoader uxtheme(L"uxtheme.dll");
    if (build < 18362)
    {
        if (auto pAllowDarkModeForApp = uxtheme.getProcedure<BOOL(WINAPI *)(BOOL)>(
                MAKEINTRESOURCEA(135)))
            pAllowDarkModeForApp(TRUE);
    }
    else
    {
        enum class PreferredAppMode : INT
        {
            Default,
            AllowDark,
            ForceDark,
            ForceLight,
            Max
        };
        if (auto pSetPreferredAppMode = uxtheme.getProcedure<INT(WINAPI *)(PreferredAppMode)>(
                MAKEINTRESOURCEA(135)))
            pSetPreferredAppMode(PreferredAppMode::AllowDark);
    }
}

/* ---------------- registry ---------------- */

struct Registry
{
    HKEY key;

    Registry() : key(nullptr)
    {
        HKEY k = nullptr;
        if (RegCreateKeyExW(HKEY_CURRENT_USER, REG_PATH, 0, nullptr, 0,
                            KEY_READ | KEY_WRITE, nullptr, &k, nullptr) == ERROR_SUCCESS)
            key = k;
    }

    ~Registry()
    {
        if (key)
            RegCloseKey(key);
    }

    Registry(const Registry &) = delete;
    Registry &operator=(const Registry &) = delete;

    [[nodiscard]] DWORD threshold() const
    {
        if (!key)
            return DEFAULT_THRESHOLD;

        DWORD value = DEFAULT_THRESHOLD;
        DWORD size = sizeof value;
        DWORD type = 0;
        if (RegQueryValueExW(key, REG_VALUE, nullptr, &type,
                             reinterpret_cast<LPBYTE>(&value), &size) != ERROR_SUCCESS
            || type != REG_DWORD)
            return DEFAULT_THRESHOLD;
        if (value > 100)
            value = 100;
        return value;
    }

    void set_threshold(DWORD value)
    {
        if (!key)
            return;
        if (value > 100)
            value = 100;
        RegSetValueExW(key, REG_VALUE, 0, REG_DWORD,
                       reinterpret_cast<const BYTE *>(&value), sizeof value);
    }

    void ensure_threshold_exists(DWORD defaultValue) const
    {
        if (!key)
            return;
        DWORD existing = 0;
        DWORD size = sizeof existing;
        DWORD type = 0;
        if (RegQueryValueExW(key, REG_VALUE, nullptr, &type,
                             reinterpret_cast<LPBYTE>(&existing), &size) == ERROR_SUCCESS
            && type == REG_DWORD)
            return;
        RegSetValueExW(key, REG_VALUE, 0, REG_DWORD,
                       reinterpret_cast<const BYTE *>(&defaultValue),
                       sizeof defaultValue);
    }
};

/* ---------------- battery query ---------------- */

static BOOL read_battery(DWORD &percent, BOOL &on_ac)
{
    SYSTEM_POWER_STATUS sps;
    if (!GetSystemPowerStatus(&sps))
        return FALSE;
    if (sps.BatteryFlag & 128)
        return FALSE;
    if (sps.BatteryLifePercent == 255)
        return FALSE;
    if (sps.ACLineStatus == 255)
        return FALSE;
    percent = sps.BatteryLifePercent;
    on_ac = (sps.ACLineStatus == 1);
    return TRUE;
}

/* ---------------- app state ---------------- */

struct app_state_t
{
    HWND            hwnd;
    HINSTANCE       hInst;
    NOTIFYICONDATAW *nid;
    HICON           app_icon;
    UINT            wm_taskbar_created;
    UINT            wm_show_menu;
    DWORD           threshold;
    BOOL            warned;
    BOOL            toast_available;
    BOOL            countdown_active;
    int             countdown_remaining;

    app_state_t(NOTIFYICONDATAW *nid_, HINSTANCE hInst_)
        : hwnd(nullptr), hInst(hInst_), nid(nid_),
          app_icon(nullptr), wm_taskbar_created(0), wm_show_menu(0),
          threshold(DEFAULT_THRESHOLD), warned(FALSE),
          toast_available(FALSE),
          countdown_active(FALSE), countdown_remaining(0) {}

    app_state_t(const app_state_t &)            = delete;
    app_state_t &operator=(const app_state_t &) = delete;

    static LRESULT CALLBACK TrayProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        auto *state = reinterpret_cast<app_state_t *>(
            GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (state) return state->handle_message(hwnd, msg, wParam, lParam);
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    /* ------- settings ------- */

    void load_settings()
    {
        Registry reg;
        threshold = reg.threshold();
    }

    void set_threshold(DWORD value)
    {
        {
            Registry reg;
            reg.set_threshold(value);
        }
        threshold = value;
        warned = FALSE;
        poll();
    }

    /* ------- tray tip ------- */

    void update_tray_tip()
    {
        DWORD percent;
        BOOL on_ac;
        wchar_t tip[128];

        if (read_battery(percent, on_ac))
        {
            wsprintfW(tip, L"%s - %lu%% (%s)", APP_TITLE,
                      static_cast<unsigned long>(percent),
                      on_ac ? L"plugged in" : L"on battery");
        }
        else
        {
            wsprintfW(tip, L"%s - no battery", APP_TITLE);
        }

        lstrcpynW(nid->szTip, tip, ARRAYSIZE(nid->szTip));
        Shell_NotifyIconW(NIM_MODIFY, nid);
    }

    /* ------- warning ------- */

    void show_warning(DWORD percent)
    {
        wchar_t body[128];
        wsprintfW(body,
                  L"Battery has dropped below the %lu%% threshold.",
                  static_cast<unsigned long>(threshold));

        wchar_t value_str[16];
        wsprintfW(value_str, L"%lu%%", static_cast<unsigned long>(percent));

        double value = static_cast<double>(percent) / 100.0;

        if (toast_available &&
            toast_show_progress(L"battery-low",
                                L"Battery low",
                                body,
                                value,
                                value_str,
                                L"Plug in the charger"))
        {
            return;
        }

        /* Fallback: classic tray balloon. */
        nid->uFlags |= NIF_INFO;
        nid->dwInfoFlags = NIIF_WARNING | NIIF_RESPECT_QUIET_TIME;
        nid->uTimeout = 15000;
        wsprintfW(nid->szInfoTitle, L"Battery at %lu%%",
                  static_cast<unsigned long>(percent));
        lstrcpynW(nid->szInfo, body, ARRAYSIZE(nid->szInfo));
        Shell_NotifyIconW(NIM_MODIFY, nid);
        nid->uFlags &= ~NIF_INFO;
        nid->dwInfoFlags = 0;
    }

    /* ------- test notification ------- */

    /* The only test entry in the menu.  It shows the toast subsystem
       in its full form: a title, a body, and a progress bar that
       updates once a second.  Users who click this see exactly the
       mechanism the real warning uses, plus the dynamic update path
       that a future "battery drains in N minutes" feature would use. */
    void start_countdown()
    {
        if (countdown_active)
            return;

        if (!toast_available)
        {
            MessageBoxW(nullptr,
                        L"Toast notifications are not available on this "
                        L"system.",
                        APP_TITLE, MB_ICONWARNING);
            return;
        }

        if (!toast_show_progress(L"countdown",
                                 L"Battery Warn",
                                 L"This is what a warning looks like. "
                                 L"The bar fills in real time.",
                                 1.0,
                                 L"60s",
                                 L"Counting down\u2026"))
        {
            MessageBoxW(nullptr,
                        L"The toast notification could not be shown.",
                        APP_TITLE, MB_ICONWARNING);
            return;
        }

        countdown_active = TRUE;
        countdown_remaining = COUNTDOWN_SECONDS;
        SetTimer(hwnd, TIMER_ID_COUNTDOWN, 1000, nullptr);
    }

    void tick_countdown()
    {
        if (!countdown_active)
            return;

        countdown_remaining--;
        if (countdown_remaining <= 0)
        {
            KillTimer(hwnd, TIMER_ID_COUNTDOWN);
            countdown_active = FALSE;
            countdown_remaining = 0;
            toast_update_progress(L"countdown", 0.0, L"0s", L"Done");
            return;
        }

        wchar_t buf[16];
        wsprintfW(buf, L"%ds", countdown_remaining);
        double value = static_cast<double>(countdown_remaining)
                     / static_cast<double>(COUNTDOWN_SECONDS);
        toast_update_progress(L"countdown", value, buf, L"Counting down\u2026");
    }

    /* ------- polling ------- */

    void poll()
    {
        DWORD percent;
        BOOL on_ac;

        if (!read_battery(percent, on_ac))
        {
            update_tray_tip();
            return;
        }

        DWORD previous_threshold = threshold;
        load_settings();
        if (threshold != previous_threshold)
            warned = FALSE;

        if (on_ac)
        {
            warned = FALSE;
        }
        else if (!warned && percent <= threshold)
        {
            show_warning(percent);
            warned = TRUE;
        }

        update_tray_tip();
    }

    /* ------- registry editor shortcut ------- */

    void open_regedit_at_key()
    {
        {
            Registry reg;
            reg.ensure_threshold_exists(threshold);
        }

        HKEY k = nullptr;
        if (RegCreateKeyExW(HKEY_CURRENT_USER,
                L"Software\\Microsoft\\Windows\\CurrentVersion\\Applets\\Regedit",
                0, nullptr, 0, KEY_WRITE, nullptr, &k, nullptr) == ERROR_SUCCESS)
        {
            constexpr const wchar_t *last_key =
                L"Computer\\HKEY_CURRENT_USER\\Software\\BatteryWarn";
            RegSetValueExW(k, L"LastKey", 0, REG_SZ,
                reinterpret_cast<const BYTE *>(last_key),
                static_cast<DWORD>((lstrlenW(last_key) + 1) * sizeof(wchar_t)));
            RegCloseKey(k);
        }

        ShellExecuteW(nullptr, L"open", L"regedit.exe", nullptr, nullptr,
                      SW_SHOWNORMAL);
    }

    /* ------- tray icon ------- */

    void add_tray_icon()
    {
        if (!app_icon)
        {
            app_icon = reinterpret_cast<HICON>(LoadImageW(hInst,
                MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON,
                GetSystemMetrics(SM_CXSMICON),
                GetSystemMetrics(SM_CYSMICON), 0));
            if (!app_icon)
                app_icon = LoadIconW(nullptr, IDI_WARNING);
        }

        nid->cbSize           = sizeof *nid;
        nid->hWnd             = hwnd;
        nid->uID              = 1;
        nid->uFlags           = NIF_MESSAGE | NIF_ICON | NIF_TIP;
        nid->uCallbackMessage = WM_TRAY;
        nid->hIcon            = app_icon;
        lstrcpynW(nid->szTip, APP_TITLE, ARRAYSIZE(nid->szTip));
        Shell_NotifyIconW(NIM_ADD, nid);
        update_tray_tip();
    }

    /* ------- menu ------- */

    void show_tray_menu()
    {
        load_settings();

        HMENU menu = CreatePopupMenu();

        DWORD percent;
        BOOL on_ac;
        wchar_t info[64];
        if (read_battery(percent, on_ac))
        {
            wsprintfW(info, L"Battery: %lu%% (%s)",
                      static_cast<unsigned long>(percent),
                      on_ac ? L"plugged in" : L"on battery");
        }
        else
        {
            lstrcpynW(info, L"Battery: unavailable", ARRAYSIZE(info));
        }
        AppendMenuW(menu, MF_STRING | MF_GRAYED, MENU_INFO, info);

        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);

        HMENU sub = CreatePopupMenu();
        struct preset_t { UINT id; DWORD value; LPCWSTR label; };
        static const preset_t presets[] = {
            { MENU_PRESET_5, 5, L"5%" },
            { MENU_PRESET_10, 10, L"10%" },
            { MENU_PRESET_15, 15, L"15%" },
            { MENU_PRESET_20, 20, L"20%" },
            { MENU_PRESET_25, 25, L"25%" },
            { MENU_PRESET_30, 30, L"30%" },
        };
        bool is_preset = false;
        for (const auto &p : presets)
        {
            bool match = (threshold == p.value);
            if (match) is_preset = true;
            AppendMenuW(sub, MF_STRING | (match ? MF_CHECKED : 0),
                        p.id, p.label);
        }
        AppendMenuW(sub, MF_SEPARATOR, 0, nullptr);
        wchar_t custom_label[64];
        wsprintfW(custom_label, L"Custom\u2026 (currently %lu%%)",
                  static_cast<unsigned long>(threshold));
        AppendMenuW(sub, MF_STRING | (is_preset ? 0 : MF_CHECKED),
                    MENU_CUSTOM, custom_label);
        AppendMenuW(menu, MF_POPUP | MF_STRING,
                    reinterpret_cast<UINT_PTR>(sub), L"Warn at");

        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, MENU_TEST, L"Test notification");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, MENU_EXIT, L"Exit");

        POINT p;
        GetCursorPos(&p);
        SetForegroundWindow(hwnd);
        TrackPopupMenu(menu, TPM_RIGHTBUTTON, p.x, p.y, 0, hwnd, nullptr);
        PostMessageW(hwnd, WM_NULL, 0, 0);
        DestroyMenu(menu);
    }

    /* ------- message dispatch ------- */

    LRESULT handle_message(HWND h, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        if (wm_taskbar_created && msg == wm_taskbar_created)
        {
            add_tray_icon();
            return 0;
        }

        if (wm_show_menu && msg == wm_show_menu)
        {
            show_tray_menu();
            return 0;
        }

        switch (msg)
        {
        case WM_TRAY:
            if (LOWORD(lParam) == WM_RBUTTONUP ||
                LOWORD(lParam) == WM_CONTEXTMENU)
                show_tray_menu();
            else if (LOWORD(lParam) == WM_LBUTTONDBLCLK)
                poll();
            return 0;

        case WM_COMMAND:
            switch (LOWORD(wParam))
            {
            case MENU_PRESET_5: set_threshold(5); return 0;
            case MENU_PRESET_10: set_threshold(10); return 0;
            case MENU_PRESET_15: set_threshold(15); return 0;
            case MENU_PRESET_20: set_threshold(20); return 0;
            case MENU_PRESET_25: set_threshold(25); return 0;
            case MENU_PRESET_30: set_threshold(30); return 0;
            case MENU_CUSTOM:    open_regedit_at_key(); return 0;
            case MENU_TEST:      start_countdown();     return 0;
            case MENU_EXIT:      DestroyWindow(h);      return 0;
            default: break;
            }
            return 0;

        case WM_TIMER:
            if (wParam == TIMER_ID)
                poll();
            else if (wParam == TIMER_ID_COUNTDOWN)
                tick_countdown();
            return 0;

        case WM_DESTROY:
            KillTimer(h, TIMER_ID);
            if (countdown_active)
            {
                KillTimer(h, TIMER_ID_COUNTDOWN);
                countdown_active = FALSE;
            }
            toast_shutdown();
            Shell_NotifyIconW(NIM_DELETE, nid);
            if (app_icon) DestroyIcon(app_icon);
            PostQuitMessage(0);
            return 0;

        default:
            break;
        }
        return DefWindowProcW(h, msg, wParam, lParam);
    }
};

/* ---------------- entry point ---------------- */

extern "C" [[noreturn]] void start();

void start()
{
    HANDLE instance_mutex = CreateMutexW(nullptr, FALSE, APP_ID);
    if (instance_mutex && GetLastError() == ERROR_ALREADY_EXISTS)
    {
        UINT msg = RegisterWindowMessageW(L"BatteryWarnShowMenu");
        if (msg) PostMessageW(HWND_BROADCAST, msg, 0, 0);
        CloseHandle(instance_mutex);
        ExitProcess(0);
    }

    enable_hidpi();
    enable_dark_mode_support();

    HINSTANCE hInst = GetModuleHandleW(nullptr);

    WNDCLASSEXW wc;
    SecureZeroMemory(&wc, sizeof wc);
    wc.cbSize        = sizeof wc;
    wc.lpfnWndProc   = app_state_t::TrayProc;
    wc.hInstance     = hInst;
    wc.lpszClassName = APP_ID;
    wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon         = reinterpret_cast<HICON>(LoadImageW(hInst,
                            MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON,
                            GetSystemMetrics(SM_CXICON),
                            GetSystemMetrics(SM_CYICON), 0));
    if (!wc.hIcon) wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wc.hIconSm       = reinterpret_cast<HICON>(LoadImageW(hInst,
                            MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON,
                            GetSystemMetrics(SM_CXSMICON),
                            GetSystemMetrics(SM_CYSMICON), 0));
    if (!wc.hIconSm) wc.hIconSm = wc.hIcon;

    if (!RegisterClassExW(&wc))
        ExitProcess(1);

    NOTIFYICONDATAW nid;
    SecureZeroMemory(&nid, sizeof nid);
    app_state_t state(&nid, hInst);
    state.wm_taskbar_created = RegisterWindowMessageW(L"TaskbarCreated");
    state.wm_show_menu       = RegisterWindowMessageW(L"BatteryWarnShowMenu");

    state.toast_available = toast_init() ? TRUE : FALSE;

    state.hwnd = CreateWindowExW(0, APP_ID, APP_TITLE, 0, 0, 0, 0, 0,
                                 nullptr, nullptr, hInst, nullptr);
    if (!state.hwnd)
        ExitProcess(1);

    SetWindowLongPtrW(state.hwnd, GWLP_USERDATA,
                      reinterpret_cast<LONG_PTR>(&state));

    state.load_settings();
    state.add_tray_icon();
    state.poll();

    SetTimer(state.hwnd, TIMER_ID, POLL_MS, nullptr);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0)
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (instance_mutex) CloseHandle(instance_mutex);
    ExitProcess(0);
}
