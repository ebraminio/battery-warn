/* toast.cc - experimental WinRT toast with a bindable progress bar. */

#include "shared.hh"
#include "toast-abi.h"

#include <shobjidl.h>
#include <propkey.h>
#include <propsys.h>

/* Windows 11 strictly requires 6 decimal places. We use pure integer math. */
static void format_unit_ratio(unsigned int num, unsigned int den, wchar_t *buf)
{
    if (den == 0 || num >= den) { lstrcpynW(buf, L"1.0", 16); return; }
    if (num == 0)               { lstrcpynW(buf, L"0.0", 16); return; }

    /* (num * 1000000) / den */
    unsigned long scaled = (static_cast<unsigned long>(num) * 1000000UL) / den;

    wsprintfW(buf, L"0.%06lu", scaled);
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
    if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
                                IID_IShellLinkW, reinterpret_cast<void **>(&sl))))
        return false;

    sl->SetPath(exe);
    sl->SetDescription(L"Battery Warn");

    IPropertyStore *ps = nullptr;
    if (SUCCEEDED(sl->QueryInterface(IID_IPropertyStore, reinterpret_cast<void **>(&ps))))
    {
        PROPVARIANT pv;
        SecureZeroMemory(&pv, sizeof pv);
        pv.vt = VT_LPWSTR;
        pv.pwszVal = const_cast<LPWSTR>(L"BatteryWarn");

        ps->SetValue(PKEY_AppUserModel_ID, pv);
        ps->Commit();
        ps->Release();
    }

    IPersistFile *pf = nullptr;
    bool ok = false;
    if (SUCCEEDED(sl->QueryInterface(IID_IPersistFile, reinterpret_cast<void **>(&pf))))
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

bool Toast::fill_values(void *data_ptr, unsigned int value_num, unsigned int value_den,
                        const wchar_t *value_string, const wchar_t *status,
                        const wchar_t *title, const wchar_t *body,
                        bool include_title_and_body)
{
    INotificationData *data = static_cast<INotificationData*>(data_ptr);
    data->put_SequenceNumber(0);

    IMap<HSTRING, HSTRING> *values = nullptr;
    if (FAILED(data->get_Values(&values)) || !values)
        return false;

    wchar_t value_buf[16];
    format_unit_ratio(value_num, value_den, value_buf);

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
                          unsigned int value_num, unsigned int value_den,
                          const wchar_t *value_string, const wchar_t *status)
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
            fill_values(data, value_num, value_den, value_string, status, title, body, true);

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

bool Toast::update_progress(const wchar_t *tag, unsigned int value_num, unsigned int value_den,
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

    fill_values(data, value_num, value_den, value_string, status, nullptr, nullptr, false);

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
