/* toast.cc - experimental WinRT toast with a bindable progress bar. */

#define WIN32_LEAN_AND_MEAN
#define UNICODE
#ifndef WINVER
#define WINVER 0x0601
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif

#include "toast.h"
#include "toast-abi.h"
#include <shobjidl.h> /* For IShellLinkW, IPropertyStore, IPersistFile */

/* Silence Clang warnings for the MSVC CRT marker */
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wreserved-identifier"
#pragma clang diagnostic ignored "-Wmissing-variable-declarations"
extern "C" int _fltused = 0;
#pragma clang diagnostic pop

/* The SDK declares these as extern const, expecting their data to come
   from uuid.lib.  Under /NODEFAULTLIB we supply our own. */
static const CLSID kCLSID_ShellLink =
    {0x00021401, 0x0000, 0x0000,
     {0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
static const IID kIID_IShellLinkW =
    {0x000214f9, 0x0000, 0x0000,
     {0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
static const IID kIID_IPersistFile =
    {0x0000010b, 0x0000, 0x0000,
     {0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
static const IID kIID_IPropertyStore =
    {0x886d8eeb, 0x8cf2, 0x4446,
     {0x8D, 0x02, 0xCD, 0xBA, 0x1D, 0xBD, 0xCF, 0x99}};

static const IID kIID_IToastNotificationManagerStatics = 
    {0x50AC103F, 0xD235, 0x4598, {0xBB, 0xEF, 0x98, 0xFE, 0x4D, 0x1A, 0x3A, 0xD4}};
static const IID kIID_IToastNotificationFactory = 
    {0x04124B20, 0x82C6, 0x4229, {0xB1, 0x09, 0xFD, 0x9E, 0xD4, 0x66, 0x2B, 0x53}};
static const IID kIID_IXmlDocument = 
    {0xF7F3A506, 0x1E87, 0x42D6, {0xBC, 0xFB, 0xB8, 0xC8, 0x09, 0xFA, 0x54, 0x94}};
static const IID kIID_IXmlDocumentIO = 
    {0x6CD0E74E, 0xEE65, 0x4489, {0x9E, 0xBF, 0xCA, 0x43, 0xE8, 0x7B, 0xA6, 0x37}};
static const IID kIID_IToastNotification2 = 
    {0x9DFB9FD1, 0x143A, 0x490E, {0x90, 0xBF, 0xB9, 0xFB, 0xA7, 0x13, 0x2D, 0xE7}};
static const IID kIID_IToastNotification4 = 
    {0x15154935, 0x28EA, 0x4727, {0x88, 0xE9, 0xC5, 0x86, 0x80, 0xE2, 0xD1, 0x18}};
static const IID kIID_IToastNotifier2 = 
    {0x354389C6, 0x7C01, 0x4BD5, {0x9C, 0x20, 0x60, 0x43, 0x4E, 0x35, 0x54, 0x9B}};
static const IID kIID_INotificationData = 
    {0x9A9D0E44, 0x2BFD, 0x44B4, {0x80, 0x44, 0x9F, 0x6A, 0xE0, 0xB1, 0xDB, 0x47}};

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
            field = reinterpret_cast<decltype(field)>(reinterpret_cast<void *>(GetProcAddress(mod, name))); \
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

/* Suppress the buffer usage warning for this small helper. */
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunsafe-buffer-usage"
static void format_unit_double(double v, wchar_t *buf)
{
    if (v >= 1.0) { buf[0] = L'1'; buf[1] = L'.'; buf[2] = L'0'; buf[3] = 0; return; }
    if (v <= 0.0) { buf[0] = L'0'; buf[1] = L'.'; buf[2] = L'0'; buf[3] = 0; return; }

    int scaled = static_cast<int>(v * 10000.0 + 0.5);
    if (scaled > 9999) scaled = 9999;
    if (scaled < 0)    scaled = 0;

    wsprintfW(buf, L"0.%04d", scaled);
}
#pragma clang diagnostic pop

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
                                kIID_IShellLinkW, reinterpret_cast<void **>(&sl))))
        return false;

    sl->SetPath(exe);
    sl->SetDescription(L"Battery Warn");

    IPropertyStore *ps = nullptr;
    if (SUCCEEDED(sl->QueryInterface(kIID_IPropertyStore, reinterpret_cast<void **>(&ps))))
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
    if (SUCCEEDED(sl->QueryInterface(kIID_IPersistFile, reinterpret_cast<void **>(&pf))))
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

    IMapStringString *values = nullptr;
    if (FAILED(data->get_Values(&values)) || !values)
        return false;

    wchar_t value_buf[16];
    format_unit_double(value, value_buf);

    unsigned char replaced = 0;
    HStr k_value(L"progressValue");      HStr v_value(value_buf);
    HStr k_vstr(L"progressValueString"); HStr v_vstr(value_string);
    HStr k_stat(L"progressStatus");      HStr v_stat(status);

    values->Insert(k_value, v_value, &replaced);
    values->Insert(k_vstr,  v_vstr,  &replaced);
    values->Insert(k_stat,  v_stat,  &replaced);

    if (include_title_and_body)
    {
        HStr k_ttl(L"toastTitle");     HStr v_ttl(title);
        HStr k_pttl(L"progressTitle"); HStr v_pttl(title);
        HStr k_bdy(L"toastBody");      HStr v_bdy(body);
        values->Insert(k_ttl,  v_ttl,  &replaced);
        values->Insert(k_pttl, v_pttl, &replaced);
        values->Insert(k_bdy,  v_bdy,  &replaced);
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
            reinterpret_cast<void **>(&mgr));
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

extern "C" bool toast_show_progress(const wchar_t *tag,
                                    const wchar_t *title,
                                    const wchar_t *body,
                                    double value,
                                    const wchar_t *value_string,
                                    const wchar_t *status)
{
    if (!g_notifier) return false;

    IInspectable *xml_inst = nullptr;
    if (FAILED(g_com.RoActivateInstance(
            HStr(L"Windows.Data.Xml.Dom.XmlDocument"), &xml_inst)))
        return false;

    IXmlDocumentIO *xml_io = nullptr;
    xml_inst->QueryInterface(kIID_IXmlDocumentIO, reinterpret_cast<void **>(&xml_io));
    if (!xml_io) { xml_inst->Release(); return false; }

    HRESULT hr = xml_io->LoadXml(HStr(TOAST_XML));
    xml_io->Release();
    if (FAILED(hr)) { xml_inst->Release(); return false; }

    IXmlDocument *xml_doc = nullptr;
    xml_inst->QueryInterface(kIID_IXmlDocument, reinterpret_cast<void **>(&xml_doc));
    xml_inst->Release();
    if (!xml_doc) return false;

    IToastNotificationFactory *factory = nullptr;
    hr = g_com.RoGetActivationFactory(
            HStr(L"Windows.UI.Notifications.ToastNotification"),
            kIID_IToastNotificationFactory,
            reinterpret_cast<void **>(&factory));
    if (FAILED(hr) || !factory) { xml_doc->Release(); return false; }

    IToastNotification *toast = nullptr;
    hr = factory->CreateToastNotification(xml_doc, &toast);
    factory->Release();
    xml_doc->Release();
    if (FAILED(hr) || !toast) return false;

    IToastNotification2 *toast2 = nullptr;
    if (SUCCEEDED(toast->QueryInterface(kIID_IToastNotification2, reinterpret_cast<void **>(&toast2))))
    {
        toast2->put_Tag(HStr(tag));
        toast2->Release();
    }

    IInspectable *data_inst = nullptr;
    if (SUCCEEDED(g_com.RoActivateInstance(
            HStr(L"Windows.UI.Notifications.NotificationData"), &data_inst)))
    {
        INotificationData *data = nullptr;
        data_inst->QueryInterface(kIID_INotificationData, reinterpret_cast<void **>(&data));
        data_inst->Release();

        if (data)
        {
            fill_values(data, value, value_string, status, title, body, true);

            IToastNotification4 *toast4 = nullptr;
            if (SUCCEEDED(toast->QueryInterface(kIID_IToastNotification4, reinterpret_cast<void **>(&toast4))))
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

extern "C" bool toast_update_progress(const wchar_t *tag,
                                      double value,
                                      const wchar_t *value_string,
                                      const wchar_t *status)
{
    if (!g_notifier || !g_current_toast) return false;

    IInspectable *data_inst = nullptr;
    if (FAILED(g_com.RoActivateInstance(
            HStr(L"Windows.UI.Notifications.NotificationData"), &data_inst)))
        return false;

    INotificationData *data = nullptr;
    data_inst->QueryInterface(kIID_INotificationData, reinterpret_cast<void **>(&data));
    data_inst->Release();
    if (!data) return false;

    fill_values(data, value, value_string, status, nullptr, nullptr, false);

    IToastNotifier2 *notifier2 = nullptr;
    HRESULT hr = g_notifier->QueryInterface(kIID_IToastNotifier2, reinterpret_cast<void **>(&notifier2));
    if (FAILED(hr) || !notifier2) { data->Release(); return false; }

    NotificationUpdateResult result = NotificationUpdateResult_Failed;
    hr = notifier2->UpdateWithTag(data, HStr(tag), reinterpret_cast<int *>(&result));
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
