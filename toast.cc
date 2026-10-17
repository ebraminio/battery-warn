/* toast.cc - experimental WinRT toast with a bindable progress bar. */

#include "toast.h"
#include "toast-abi.h"

#define WIN32_LEAN_AND_MEAN
#define UNICODE
#define _UNICODE
#ifndef WINVER
#define WINVER 0x0601
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif

#include <Windows.h>
#include <shobjidl.h>
#include <propkey.h>
#include <propsys.h>

extern "C" int _fltused = 0;

static const CLSID kCLSID_ShellLink =
    {0x00021401, 0x0000, 0x0000, {0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
static const IID kIID_IShellLinkW =
    {0x000214f9, 0x0000, 0x0000, {0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
static const IID kIID_IPersistFile =
    {0x0000010b, 0x0000, 0x0000, {0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
static const IID kIID_IPropertyStore =
    {0x886d8eeb, 0x8cf2, 0x4446, {0x8D, 0x02, 0xCD, 0xBA, 0x1D, 0xBD, 0xCF, 0x99}};

struct Combase
{
    HMODULE mod;
    HRESULT (WINAPI *RoInitialize)(int);
    void    (WINAPI *RoUninitialize)();
    HRESULT (WINAPI *RoGetActivationFactory)(HSTRING, REFIID, void **);
    HRESULT (WINAPI *RoActivateInstance)(HSTRING, IInspectable **);
    HRESULT (WINAPI *WindowsCreateString)(LPCWSTR, UINT32, HSTRING *);
    HRESULT (WINAPI *WindowsDeleteString)(HSTRING);

    bool load()
    {
        mod = GetModuleHandleW(L"combase.dll");
        if (!mod) mod = LoadLibraryW(L"combase.dll");
        if (!mod) return false;

        #define GRAB(field, name) \
            field = reinterpret_cast<decltype(field)>(GetProcAddress(mod, name)); \
            if (!field) return false;

        GRAB(RoInitialize, "RoInitialize")
        GRAB(RoUninitialize, "RoUninitialize")
        GRAB(RoGetActivationFactory, "RoGetActivationFactory")
        GRAB(RoActivateInstance, "RoActivateInstance")
        GRAB(WindowsCreateString, "WindowsCreateString")
        GRAB(WindowsDeleteString, "WindowsDeleteString")
        #undef GRAB
        return true;
    }
};

static Combase g_com;

struct HStr
{
    HSTRING h;
    HStr(const wchar_t *s) : h(nullptr)
    {
        if (s) g_com.WindowsCreateString(s, static_cast<UINT32>(lstrlenW(s)), &h);
    }
    ~HStr() { if (h) g_com.WindowsDeleteString(h); }
    operator HSTRING() const { return h; }
    HStr(const HStr &) = delete;
    HStr &operator=(const HStr &) = delete;
};

static IToastNotifier     *g_notifier      = nullptr;
static IToastNotification *g_current_toast = nullptr;

static void format_unit_double(double v, wchar_t *buf)
{
    if (v >= 1.0) { lstrcpynW(buf, L"1.0", 16); return; }
    if (v <= 0.0) { lstrcpynW(buf, L"0.0", 16); return; }

    int scaled = static_cast<int>(v * 1000000.0 + 0.5);
    if (scaled > 999999) scaled = 999999;
    if (scaled < 0)      scaled = 0;

    wsprintfW(buf, L"0.%06d", scaled);
}

static constexpr LPCWSTR TOAST_XML =
    L"<toast>"
    L"  <visual>"
    L"    <binding template='ToastGeneric'>"
    L"      <text>{toastTitle}</text>"
    L"      <text>{toastBody}</text>"
    L"      <progress"
    L"        title='{progressTitle}'"
    L"        value='{progressValue}'"
    L"        valueStringOverride='{progressValueString}'"
    L"        status='{progressStatus}' />"
    L"    </binding>"
    L"  </visual>"
    L"</toast>";

static bool register_aumid()
{
    wchar_t appdata[MAX_PATH];
    if (!GetEnvironmentVariableW(L"APPDATA", appdata, MAX_PATH))
        return false;

    wchar_t programs[MAX_PATH];
    wsprintfW(programs, L"%s\\Microsoft\\Windows\\Start Menu\\Programs", appdata);

    wchar_t lnk[MAX_PATH];
    wsprintfW(lnk, L"%s\\BatteryWarn.lnk", programs);

    wchar_t exe[MAX_PATH];
    if (!GetModuleFileNameW(nullptr, exe, MAX_PATH))
        return false;

    IShellLinkW *sl = nullptr;
    if (FAILED(CoCreateInstance(kCLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
                                kIID_IShellLinkW, (void **)&sl)))
        return false;

    sl->SetPath(exe);
    sl->SetDescription(L"Battery Warn");

    IPropertyStore *ps = nullptr;
    if (SUCCEEDED(sl->QueryInterface(kIID_IPropertyStore, (void **)&ps)))
    {
        PROPERTYKEY pkey;
        pkey.fmtid = {0x9F4C2855, 0x9F79, 0x4B39, {0xA8, 0xD0, 0xE1, 0xD4, 0x2D, 0xE1, 0xD5, 0xF3}};
        pkey.pid = 5;

        PROPVARIANT pv;
        SecureZeroMemory(&pv, sizeof pv);
        pv.vt = VT_LPWSTR;
        pv.pwszVal = const_cast<LPWSTR>(L"BatteryWarn");

        ps->SetValue(pkey, pv);
        ps->Commit();
        ps->Release();
    }

    IPersistFile *pf = nullptr;
    bool ok = false;
    if (SUCCEEDED(sl->QueryInterface(kIID_IPersistFile, (void **)&pf)))
    {
        ok = SUCCEEDED(pf->Save(lnk, TRUE));
        pf->Release();
    }
    sl->Release();

    HKEY k = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER,
            L"Software\\Classes\\AppUserModelId\\BatteryWarn",
            0, nullptr, 0, KEY_WRITE, nullptr, &k, nullptr) == ERROR_SUCCESS)
    {
        RegSetValueExW(k, L"DisplayName", 0, REG_SZ,
                       reinterpret_cast<const BYTE *>(L"Battery Warn"),
                       sizeof L"Battery Warn");
        RegSetValueExW(k, L"IconUri", 0, REG_SZ,
                       reinterpret_cast<const BYTE *>(exe),
                       static_cast<DWORD>((static_cast<size_t>(lstrlenW(exe)) + 1) * sizeof(wchar_t)));
        RegCloseKey(k);
    }

    return ok;
}

static bool fill_values(INotificationData *data,
                        double value,
                        const wchar_t *value_string,
                        const wchar_t *status,
                        const wchar_t *title,
                        const wchar_t *body,
                        bool include_title_and_body)
{
    data->put_SequenceNumber(0);

    IMap<HSTRING, HSTRING> *values = nullptr;
    if (FAILED(data->get_Values(&values)) || !values)
        return false;

    wchar_t value_buf[16];
    format_unit_double(value, value_buf);

    unsigned char replaced = 0;
    HStr k_value(L"progressValue");      HStr v_value(value_buf);
    HStr k_vstr(L"progressValueString"); HStr v_vstr(value_string);
    HStr k_stat(L"progressStatus");      HStr v_stat(status);

    if (FAILED(values->Insert(k_value, v_value, &replaced))) { values->Release(); return false; }
    if (FAILED(values->Insert(k_vstr,  v_vstr,  &replaced))) { values->Release(); return false; }
    if (FAILED(values->Insert(k_stat,  v_stat,  &replaced))) { values->Release(); return false; }

    if (include_title_and_body)
    {
        HStr k_ttl(L"toastTitle");     HStr v_ttl(title);
        HStr k_pttl(L"progressTitle"); HStr v_pttl(title);
        HStr k_bdy(L"toastBody");      HStr v_bdy(body);
        if (FAILED(values->Insert(k_ttl,  v_ttl,  &replaced))) { values->Release(); return false; }
        if (FAILED(values->Insert(k_pttl, v_pttl, &replaced))) { values->Release(); return false; }
        if (FAILED(values->Insert(k_bdy,  v_bdy,  &replaced))) { values->Release(); return false; }
    }

    values->Release();
    return true;
}

extern "C" bool toast_init()
{
    if (!g_com.load())
        return false;

    HRESULT hr = g_com.RoInitialize(1);
    if (FAILED(hr) && hr != S_FALSE && hr != RPC_E_CHANGED_MODE)
        return false;

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    SetCurrentProcessExplicitAppUserModelID(L"BatteryWarn");
    register_aumid();

    IToastNotificationManagerStatics *mgr = nullptr;
    hr = g_com.RoGetActivationFactory(
            HStr(L"Windows.UI.Notifications.ToastNotificationManager"),
            kIID_IToastNotificationManagerStatics,
            (void **)&mgr);
    if (FAILED(hr) || !mgr) return false;

    hr = mgr->CreateToastNotifierWithId(HStr(L"BatteryWarn"), &g_notifier);
    mgr->Release();

    return SUCCEEDED(hr) && g_notifier != nullptr;
}

extern "C" void toast_shutdown()
{
    if (g_current_toast) { g_current_toast->Release(); g_current_toast = nullptr; }
    if (g_notifier)      { g_notifier->Release();      g_notifier = nullptr; }
}

extern "C" bool toast_show_progress(const wchar_t *tag, const wchar_t *title, const wchar_t *body,
                                    double value, const wchar_t *value_string, const wchar_t *status)
{
    if (!g_notifier) return false;

    IInspectable *xml_inst = nullptr;
    if (FAILED(g_com.RoActivateInstance(
            HStr(L"Windows.Data.Xml.Dom.XmlDocument"), &xml_inst)))
        return false;

    IXmlDocumentIO *xml_io = nullptr;
    xml_inst->QueryInterface(kIID_IXmlDocumentIO, (void **)&xml_io);
    if (!xml_io) { xml_inst->Release(); return false; }

    HRESULT hr = xml_io->LoadXml(HStr(TOAST_XML));
    xml_io->Release();
    if (FAILED(hr)) { xml_inst->Release(); return false; }

    IXmlDocument *xml_doc = nullptr;
    xml_inst->QueryInterface(kIID_IXmlDocument, (void **)&xml_doc);
    xml_inst->Release();
    if (!xml_doc) return false;

    IToastNotificationFactory *factory = nullptr;
    hr = g_com.RoGetActivationFactory(
            HStr(L"Windows.UI.Notifications.ToastNotification"),
            kIID_IToastNotificationFactory,
            (void **)&factory);
    if (FAILED(hr) || !factory) { xml_doc->Release(); return false; }

    IToastNotification *toast = nullptr;
    hr = factory->CreateToastNotification(xml_doc, &toast);
    factory->Release();
    xml_doc->Release();
    if (FAILED(hr) || !toast) return false;

    IToastNotification2 *toast2 = nullptr;
    if (SUCCEEDED(toast->QueryInterface(kIID_IToastNotification2, (void **)&toast2)))
    {
        toast2->put_Tag(HStr(tag));
        toast2->Release();
    }

    IInspectable *data_inst = nullptr;
    if (SUCCEEDED(g_com.RoActivateInstance(
            HStr(L"Windows.UI.Notifications.NotificationData"), &data_inst)))
    {
        INotificationData *data = nullptr;
        data_inst->QueryInterface(kIID_INotificationData, (void **)&data);
        data_inst->Release();

        if (data)
        {
            fill_values(data, value, value_string, status, title, body, true);

            IToastNotification4 *toast4 = nullptr;
            if (SUCCEEDED(toast->QueryInterface(kIID_IToastNotification4, (void **)&toast4)))
            {
                toast4->put_Data(data);
                toast4->Release();
            }
            data->Release();
        }
    }

    hr = g_notifier->Show(toast);
    if (FAILED(hr)) { toast->Release(); return false; }

    if (g_current_toast) g_current_toast->Release();
    g_current_toast = toast;
    return true;
}

extern "C" bool toast_update_progress(const wchar_t *tag, double value,
                                      const wchar_t *value_string, const wchar_t *status)
{
    if (!g_notifier || !g_current_toast) return false;

    IInspectable *data_inst = nullptr;
    if (FAILED(g_com.RoActivateInstance(
            HStr(L"Windows.UI.Notifications.NotificationData"), &data_inst)))
        return false;

    INotificationData *data = nullptr;
    data_inst->QueryInterface(kIID_INotificationData, (void **)&data);
    data_inst->Release();
    if (!data) return false;

    fill_values(data, value, value_string, status, nullptr, nullptr, false);

    IToastNotifier2 *notifier2 = nullptr;
    HRESULT hr = g_notifier->QueryInterface(kIID_IToastNotifier2, (void **)&notifier2);
    if (FAILED(hr) || !notifier2) { data->Release(); return false; }

    NotificationUpdateResult result = NotificationUpdateResult_Failed;
    hr = notifier2->UpdateWithTag(data, HStr(tag), &result);
    notifier2->Release();
    data->Release();

    return SUCCEEDED(hr) && result == NotificationUpdateResult_Succeeded;
}

extern "C" bool toast_hide(const wchar_t * /*tag*/)
{
    if (!g_notifier || !g_current_toast) return false;
    HRESULT hr = g_notifier->Hide(g_current_toast);
    g_current_toast->Release();
    g_current_toast = nullptr;
    return SUCCEEDED(hr);
}
