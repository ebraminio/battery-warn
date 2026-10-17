/* toast.cc - experimental WinRT toast with a bindable progress bar. */

#include "shared.hh"
#include "toast-abi.h"

#include <shobjidl.h>
#include <propkey.h>
#include <propsys.h>

#if defined(_MSC_VER)
extern "C" int _fltused = 0;
#endif

static const CLSID kCLSID_ShellLink =
    {0x00021401, 0x0000, 0x0000, {0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
static const IID kIID_IShellLinkW =
    {0x000214f9, 0x0000, 0x0000, {0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
static const IID kIID_IPersistFile =
    {0x0000010b, 0x0000, 0x0000, {0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
static const IID kIID_IPropertyStore =
    {0x886d8eeb, 0x8cf2, 0x4446, {0x8D, 0x02, 0xCD, 0xBA, 0x1D, 0xBD, 0xCF, 0x99}};

/* Windows 11 strictly requires 6 decimal places for the progress bar float. */
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

/* -------- Toast class implementation -------- */

Toast::HStr::HStr(Toast *state_, const wchar_t *s) : h(nullptr), state(state_)
{
    if (s) state->pWindowsCreateString_(s, static_cast<UINT32>(lstrlenW(s)), &h);
}

Toast::HStr::~HStr()
{
    if (h && state->pWindowsDeleteString_) state->pWindowsDeleteString_(h);
}

Toast::Toast()
    : notifier_(nullptr), current_toast_(nullptr), mod_(nullptr),
      pRoInitialize_(nullptr), pRoUninitialize_(nullptr), pRoGetActivationFactory_(nullptr),
      pRoActivateInstance_(nullptr), pWindowsCreateString_(nullptr), pWindowsDeleteString_(nullptr)
{
}

Toast::~Toast()
{
    shutdown();
}

void Toast::shutdown()
{
    if (current_toast_) { static_cast<IToastNotification*>(current_toast_)->Release(); current_toast_ = nullptr; }
    if (notifier_)      { static_cast<IToastNotifier*>(notifier_)->Release();      notifier_ = nullptr; }
}

bool Toast::init()
{
    mod_ = GetModuleHandleW(L"combase.dll");
    if (!mod_) mod_ = LoadLibraryW(L"combase.dll");
    if (!mod_) return false;

    #define GRAB(field, name) \
        field = reinterpret_cast<decltype(field)>(reinterpret_cast<void *>(GetProcAddress(mod_, name))); \
        if (!field) return false;

    GRAB(pRoInitialize_, "RoInitialize")
    GRAB(pRoUninitialize_, "RoUninitialize")
    GRAB(pRoGetActivationFactory_, "RoGetActivationFactory")
    GRAB(pRoActivateInstance_, "RoActivateInstance")
    GRAB(pWindowsCreateString_, "WindowsCreateString")
    GRAB(pWindowsDeleteString_, "WindowsDeleteString")
    #undef GRAB

    HRESULT hr = pRoInitialize_(1);
    if (FAILED(hr) && hr != S_FALSE && hr != RPC_E_CHANGED_MODE)
        return false;

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    SetCurrentProcessExplicitAppUserModelID(L"BatteryWarn");
    register_aumid();

    IToastNotificationManagerStatics *mgr = nullptr;
    hr = pRoGetActivationFactory_(
            HStr(this, L"Windows.UI.Notifications.ToastNotificationManager"),
            kIID_IToastNotificationManagerStatics,
            reinterpret_cast<void **>(&mgr));
    if (FAILED(hr) || !mgr) return false;

    hr = mgr->CreateToastNotifierWithId(HStr(this, L"BatteryWarn"), reinterpret_cast<IToastNotifier**>(&notifier_));
    mgr->Release();

    return SUCCEEDED(hr) && notifier_ != nullptr;
}

bool Toast::fill_values(void *data_ptr, double value, const wchar_t *value_string,
                        const wchar_t *status, const wchar_t *title,
                        const wchar_t *body, bool include_title_and_body)
{
    INotificationData *data = static_cast<INotificationData*>(data_ptr);
    data->put_SequenceNumber(0);

    IMap<HSTRING, HSTRING> *values = nullptr;
    if (FAILED(data->get_Values(&values)) || !values)
        return false;

    wchar_t value_buf[16];
    format_unit_double(value, value_buf);

    unsigned char replaced = 0;
    HStr k_value(this, L"progressValue");      HStr v_value(this, value_buf);
    HStr k_vstr(this, L"progressValueString"); HStr v_vstr(this, value_string);
    HStr k_stat(this, L"progressStatus");      HStr v_stat(this, status);

    if (FAILED(values->Insert(k_value, v_value, &replaced))) { values->Release(); return false; }
    if (FAILED(values->Insert(k_vstr,  v_vstr,  &replaced))) { values->Release(); return false; }
    if (FAILED(values->Insert(k_stat,  v_stat,  &replaced))) { values->Release(); return false; }

    if (include_title_and_body)
    {
        HStr k_ttl(this, L"toastTitle");     HStr v_ttl(this, title);
        HStr k_pttl(this, L"progressTitle"); HStr v_pttl(this, title);
        HStr k_bdy(this, L"toastBody");      HStr v_bdy(this, body);
        if (FAILED(values->Insert(k_ttl,  v_ttl,  &replaced))) { values->Release(); return false; }
        if (FAILED(values->Insert(k_pttl, v_pttl, &replaced))) { values->Release(); return false; }
        if (FAILED(values->Insert(k_bdy,  v_bdy,  &replaced))) { values->Release(); return false; }
    }

    values->Release();
    return true;
}

bool Toast::show_progress(const wchar_t *tag, const wchar_t *title, const wchar_t *body,
                          double value, const wchar_t *value_string, const wchar_t *status)
{
    if (!notifier_) return false;
    IToastNotifier *notifier = static_cast<IToastNotifier*>(notifier_);

    void *xml_inst = nullptr;
    if (FAILED(pRoActivateInstance_(
            HStr(this, L"Windows.Data.Xml.Dom.XmlDocument"), &xml_inst)))
        return false;

    IXmlDocumentIO *xml_io = nullptr;
    static_cast<IInspectable*>(xml_inst)->QueryInterface(kIID_IXmlDocumentIO, reinterpret_cast<void **>(&xml_io));
    if (!xml_io) { static_cast<IInspectable*>(xml_inst)->Release(); return false; }

    HRESULT hr = xml_io->LoadXml(HStr(this, TOAST_XML));
    xml_io->Release();
    if (FAILED(hr)) { static_cast<IInspectable*>(xml_inst)->Release(); return false; }

    IXmlDocument *xml_doc = nullptr;
    static_cast<IInspectable*>(xml_inst)->QueryInterface(kIID_IXmlDocument, reinterpret_cast<void **>(&xml_doc));
    static_cast<IInspectable*>(xml_inst)->Release();
    if (!xml_doc) return false;

    IToastNotificationFactory *factory = nullptr;
    hr = pRoGetActivationFactory_(
            HStr(this, L"Windows.UI.Notifications.ToastNotification"),
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
        toast2->put_Tag(HStr(this, tag));
        toast2->Release();
    }

    void *data_inst = nullptr;
    if (SUCCEEDED(pRoActivateInstance_(
            HStr(this, L"Windows.UI.Notifications.NotificationData"), &data_inst)))
    {
        INotificationData *data = nullptr;
        static_cast<IInspectable*>(data_inst)->QueryInterface(kIID_INotificationData, reinterpret_cast<void **>(&data));
        static_cast<IInspectable*>(data_inst)->Release();

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

    hr = notifier->Show(toast);
    if (FAILED(hr)) { toast->Release(); return false; }

    if (current_toast_) static_cast<IToastNotification*>(current_toast_)->Release();
    current_toast_ = toast;
    return true;
}

bool Toast::update_progress(const wchar_t *tag, double value,
                            const wchar_t *value_string, const wchar_t *status)
{
    if (!notifier_ || !current_toast_) return false;
    IToastNotifier *notifier = static_cast<IToastNotifier*>(notifier_);

    void *data_inst = nullptr;
    if (FAILED(pRoActivateInstance_(
            HStr(this, L"Windows.UI.Notifications.NotificationData"), &data_inst)))
        return false;

    INotificationData *data = nullptr;
    static_cast<IInspectable*>(data_inst)->QueryInterface(kIID_INotificationData, reinterpret_cast<void **>(&data));
    static_cast<IInspectable*>(data_inst)->Release();
    if (!data) return false;

    fill_values(data, value, value_string, status, nullptr, nullptr, false);

    IToastNotifier2 *notifier2 = nullptr;
    HRESULT hr = notifier->QueryInterface(kIID_IToastNotifier2, reinterpret_cast<void **>(&notifier2));
    if (FAILED(hr) || !notifier2) { data->Release(); return false; }

    NotificationUpdateResult result = NotificationUpdateResult_Failed;
    hr = notifier2->UpdateWithTag(data, HStr(this, tag), &result);
    notifier2->Release();
    data->Release();

    return SUCCEEDED(hr) && result == NotificationUpdateResult_Succeeded;
}

bool Toast::hide(const wchar_t * /*tag*/)
{
    if (!notifier_ || !current_toast_) return false;
    IToastNotifier *notifier = static_cast<IToastNotifier*>(notifier_);
    IToastNotification *toast = static_cast<IToastNotification*>(current_toast_);

    HRESULT hr = notifier->Hide(toast);
    toast->Release();
    current_toast_ = nullptr;
    return SUCCEEDED(hr);
}
