#pragma once

// =============================================================================
// Minimal ABI-compatible declarations for a small subset of
//   Windows.UI.Notifications.* and Windows.Foundation.Collections.*
//
// Notes / caveats
// ---------------
// *  WinRT interfaces are SIBLINGS at the ABI, not a C++ inheritance chain.
//    IToastNotification2/3/4 each derive from IInspectable only; the runtime
//    class implements all of them and you QI for the one you need.
//    Similarly, IMap<K,V> does NOT derive from IIterable<...>; the runtime
//    Map class *also* implements IIterable<IKeyValuePair<K,V>> but that is a
//    separate interface with its own IID and its own vtable.
//
// *  This header is intended to be compiled alone in a TU that does NOT also
//    pull in <inspectable.h>, <rpcndr.h>, <winstring.h>, <objidl.h>, etc.
//    If any of those are visible, delete the corresponding stand-in
//    typedefs/struct below.  <windows.h> is included with WIN32_LEAN_AND_MEAN
//    specifically to keep <ole2.h>/<rpc.h>/<inspectable.h> out of the TU.
// =============================================================================

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#ifndef STDMETHODCALLTYPE
#define STDMETHODCALLTYPE __stdcall
#endif

// -----------------------------------------------------------------------------
// ABI-compatible stand-ins for the WinRT base types.
// -----------------------------------------------------------------------------
typedef void*         HSTRING;                       // single pointer
typedef unsigned char boolean;                       // one byte
struct EventRegistrationToken { __int64 value; };    // 8 bytes (was "int" before)

// -----------------------------------------------------------------------------
// IInspectable — identical vtable shape to the real one.
// -----------------------------------------------------------------------------
struct IInspectable {
    virtual HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) = 0;
    virtual ULONG   STDMETHODCALLTYPE AddRef() = 0;
    virtual ULONG   STDMETHODCALLTYPE Release() = 0;
    virtual HRESULT STDMETHODCALLTYPE GetIids(ULONG* iidCount, IID** iids) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetRuntimeClassName(HSTRING* className) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetTrustLevel(int* trustLevel) = 0;
};

// -----------------------------------------------------------------------------
// Enums (values match the Windows SDK).
// -----------------------------------------------------------------------------
enum NotificationUpdateResult {
    NotificationUpdateResult_Succeeded = 0,
    NotificationUpdateResult_Failed = 1,
    NotificationUpdateResult_NotificationNotFound = 2,  // real name is NOT "_NotFound"
};

enum NotificationSetting {
    NotificationSetting_Enabled = 0,
    NotificationSetting_DisabledForApplication = 1,
    NotificationSetting_DisabledForUser = 2,
    NotificationSetting_DisabledByGroupPolicy = 3,
    NotificationSetting_DisabledByManifest = 4,
};

enum ToastTemplateType {
    ToastTemplateType_ToastImageAndText01 = 0,
    ToastTemplateType_ToastImageAndText02 = 1,
    ToastTemplateType_ToastImageAndText03 = 2,
    ToastTemplateType_ToastImageAndText04 = 3,
    ToastTemplateType_ToastText01 = 4,
    ToastTemplateType_ToastText02 = 5,
    ToastTemplateType_ToastText03 = 6,
    ToastTemplateType_ToastText04 = 7,
};

// RoInitialize init types (in the SDK these are #defines, not an enum).
enum { RO_INIT_SINGLETHREADED = 0, RO_INIT_MULTITHREADED = 1 };

// -----------------------------------------------------------------------------
// Opaque interfaces we only pass around (methods intentionally unused).
// If you need the full vtable for any of these, add the methods in SDK order.
// -----------------------------------------------------------------------------
struct IScheduledToastNotification : IInspectable {};
struct IScheduledToastNotificationShowingEventArgs : IInspectable {};
struct IToastNotificationHistory : IInspectable {};

// -----------------------------------------------------------------------------
// XML DOM.
// NOTE: only the methods below are correct up to the last declared method.
// IXmlDocument itself has many more methods after QueryInterface/AddRef/Release/
// GetIids/GetRuntimeClassName/GetTrustLevel; they are not needed here because
// we only hand the interface pointer around opaquely.
// -----------------------------------------------------------------------------
struct IXmlDocument : IInspectable {};

struct IXmlDocumentIO : IInspectable {
    // Slot 6 of the real IXmlDocumentIO. (LoadXmlWithSettings and
    // SaveToFileAsync would be slots 7 and 8 if you ever need them.)
    virtual HRESULT STDMETHODCALLTYPE LoadXml(HSTRING xml) = 0;
};

// -----------------------------------------------------------------------------
// Collections.
//
// CRITICAL: IMap<K,V> derives from IInspectable ONLY. The runtime Map class
// also implements IIterable<IKeyValuePair<K,V>>, but that is a SEPARATE
// interface with its own IID (e9bdaaf0-cbf6-5c72-be90-29cbf3a1319b). QI-ing for
// IMap<HSTRING,HSTRING> yields a vtable whose slot 6 is Lookup — not First.
// -----------------------------------------------------------------------------
template <typename T>
struct IIterable : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE First(void** first) = 0;  // IIterator<T>**
};

template <typename K, typename V>
struct IKeyValuePair : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_Key(K* key) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Value(V* value) = 0;
};

template <typename K, typename V>
struct IMap : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE Lookup(K key, V* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Size(UINT32* size) = 0;
    virtual HRESULT STDMETHODCALLTYPE HasKey(K key, boolean* found) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetView(void** view) = 0;   // IMapView<K,V>**
    virtual HRESULT STDMETHODCALLTYPE Insert(K key, V value, boolean* replaced) = 0;
    virtual HRESULT STDMETHODCALLTYPE Remove(K key) = 0;
    virtual HRESULT STDMETHODCALLTYPE Clear() = 0;
};

// -----------------------------------------------------------------------------
// INotificationData + its activation factory.
// -----------------------------------------------------------------------------
struct INotificationData : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_Values(IMap<HSTRING, HSTRING>** value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_SequenceNumber(UINT32* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_SequenceNumber(UINT32 value) = 0;
};

struct INotificationDataFactory : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE CreateNotificationDataWithValuesAndSequenceNumber(
        IIterable<IKeyValuePair<HSTRING, HSTRING>*>* initialValues,
        UINT32 sequenceNumber,
        INotificationData** value) = 0;

    virtual HRESULT STDMETHODCALLTYPE CreateNotificationDataWithValues(
        IIterable<IKeyValuePair<HSTRING, HSTRING>*>* initialValues,
        INotificationData** value) = 0;
};

// -----------------------------------------------------------------------------
// IToastNotification (9 methods, SDK order).
// -----------------------------------------------------------------------------
struct IToastNotification : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_Content(IXmlDocument** value) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_ExpirationTime(void* value) = 0;    // IReference<DateTime>*
    virtual HRESULT STDMETHODCALLTYPE get_ExpirationTime(void** value) = 0;   // IReference<DateTime>**
    virtual HRESULT STDMETHODCALLTYPE add_Dismissed(void* handler, EventRegistrationToken* token) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_Dismissed(EventRegistrationToken token) = 0;
    virtual HRESULT STDMETHODCALLTYPE add_Activated(void* handler, EventRegistrationToken* token) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_Activated(EventRegistrationToken token) = 0;
    virtual HRESULT STDMETHODCALLTYPE add_Failed(void* handler, EventRegistrationToken* token) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_Failed(EventRegistrationToken token) = 0;
};

// Sibling of IToastNotification (NOT a subclass at the ABI).
struct IToastNotification2 : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE put_Tag(HSTRING value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Tag(HSTRING* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_Group(HSTRING value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Group(HSTRING* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_SuppressPopup(boolean value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_SuppressPopup(boolean* value) = 0;
};

// Sibling.
struct IToastNotification3 : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_NotificationMirroring(int* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_NotificationMirroring(int value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_RemoteId(HSTRING* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_RemoteId(HSTRING value) = 0;
};

// Sibling.
struct IToastNotification4 : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_Data(INotificationData** value) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_Data(INotificationData* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Priority(int* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_Priority(int value) = 0;
};

// -----------------------------------------------------------------------------
// IToastNotifier (6 methods, SDK order).
// -----------------------------------------------------------------------------
struct IToastNotifier : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE Show(IToastNotification* notification) = 0;
    virtual HRESULT STDMETHODCALLTYPE Hide(IToastNotification* notification) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Setting(int* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE AddToSchedule(IScheduledToastNotification* scheduledToast) = 0;
    virtual HRESULT STDMETHODCALLTYPE RemoveFromSchedule(IScheduledToastNotification* scheduledToast) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetScheduledToastNotifications(void** result) = 0;  // IVectorView<...>**
};

// Sibling of IToastNotifier (NOT a subclass at the ABI).
struct IToastNotifier2 : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE UpdateWithTagAndGroup(
        INotificationData* data, HSTRING tag, HSTRING group,
        NotificationUpdateResult* result) = 0;

    virtual HRESULT STDMETHODCALLTYPE UpdateWithTag(
        INotificationData* data, HSTRING tag,
        NotificationUpdateResult* result) = 0;
};

// Sibling.
struct IToastNotifier3 : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE add_ScheduledToastNotificationShowing(
        void* handler, EventRegistrationToken* token) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_ScheduledToastNotificationShowing(
        EventRegistrationToken token) = 0;
};

// -----------------------------------------------------------------------------
// Activation factories / statics.
// -----------------------------------------------------------------------------
struct IToastNotificationFactory : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE CreateToastNotification(
        IXmlDocument* content, IToastNotification** value) = 0;
};

struct IToastNotificationManagerStatics : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE CreateToastNotifier(IToastNotifier** result) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateToastNotifierWithId(
        HSTRING applicationId, IToastNotifier** result) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetTemplateContent(
        ToastTemplateType type, IXmlDocument** result) = 0;
};

struct IToastNotificationManagerStatics2 : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_History(IToastNotificationHistory** value) = 0;
};

// -----------------------------------------------------------------------------
// IIDs (copied verbatim from the real SDK headers).
// -----------------------------------------------------------------------------
static const IID kIID_IXmlDocument                      = { 0xF7F3A506, 0x1E87, 0x42D6, { 0xBC, 0xFB, 0xB8, 0xC8, 0x09, 0xFA, 0x54, 0x94 } };
static const IID kIID_IXmlDocumentIO                    = { 0x6CD0E74E, 0xEE65, 0x4489, { 0x9E, 0xBF, 0xCA, 0x43, 0xE8, 0x7B, 0xA6, 0x37 } };
static const IID kIID_IKeyValuePair_HSTRING_HSTRING     = { 0x60310303, 0x49C5, 0x52E6, { 0xAB, 0xC6, 0xA9, 0xB3, 0x6E, 0xCC, 0xC7, 0x16 } };
static const IID kIID_IMap_HSTRING_HSTRING              = { 0xF6D1F700, 0x49C2, 0x52AE, { 0x81, 0x54, 0x82, 0x6F, 0x99, 0x08, 0x77, 0x3C } };
static const IID kIID_INotificationData                 = { 0x9FFD2312, 0x9D6A, 0x4AAF, { 0xB6, 0xAC, 0xFF, 0x17, 0xF0, 0xC1, 0xF2, 0x80 } };
static const IID kIID_INotificationDataFactory          = { 0x23C1E33A, 0x1C10, 0x46FB, { 0x80, 0x40, 0xDE, 0xC3, 0x84, 0x62, 0x1C, 0xF8 } };
static const IID kIID_IToastNotification                = { 0x997E2675, 0x059E, 0x4E60, { 0x8B, 0x06, 0x17, 0x60, 0x91, 0x7C, 0x8B, 0x80 } };
static const IID kIID_IToastNotification2               = { 0x9DFB9FD1, 0x143A, 0x490E, { 0x90, 0xBF, 0xB9, 0xFB, 0xA7, 0x13, 0x2D, 0xE7 } };
static const IID kIID_IToastNotification3               = { 0x31E8AED8, 0x8141, 0x4F99, { 0xBC, 0x0A, 0xC4, 0xED, 0x21, 0x29, 0x7D, 0x77 } };
static const IID kIID_IToastNotification4               = { 0x15154935, 0x28EA, 0x4727, { 0x88, 0xE9, 0xC5, 0x86, 0x80, 0xE2, 0xD1, 0x18 } };
static const IID kIID_IToastNotifier                    = { 0x75927B93, 0x03F3, 0x41EC, { 0x91, 0xD3, 0x6E, 0x5B, 0xAC, 0x1B, 0x38, 0xE7 } };
static const IID kIID_IToastNotifier2                   = { 0x354389C6, 0x7C01, 0x4BD5, { 0x9C, 0x20, 0x60, 0x43, 0x40, 0xCD, 0x2B, 0x74 } };
static const IID kIID_IToastNotifier3                   = { 0xAE75A04A, 0x3B0C, 0x51AD, { 0xB7, 0xE8, 0xB0, 0x8A, 0xB6, 0x05, 0x25, 0x49 } };
static const IID kIID_IToastNotificationFactory         = { 0x04124B20, 0x82C6, 0x4229, { 0xB1, 0x09, 0xFD, 0x9E, 0xD4, 0x66, 0x2B, 0x53 } };
static const IID kIID_IToastNotificationManagerStatics  = { 0x50AC103F, 0xD235, 0x4598, { 0xBB, 0xEF, 0x98, 0xFE, 0x4D, 0x1A, 0x3A, 0xD4 } };
static const IID kIID_IToastNotificationManagerStatics2 = { 0x7AB93C52, 0x0E48, 0x4750, { 0xBA, 0x9D, 0x1A, 0x41, 0x13, 0x98, 0x18, 0x47 } };

// -----------------------------------------------------------------------------
// WinRT activation / string API.
// -----------------------------------------------------------------------------
extern "C" {
    HRESULT WINAPI RoInitialize(int initType);
    void    WINAPI RoUninitialize();
    HRESULT WINAPI RoGetActivationFactory(HSTRING activatableClassId, REFIID iid, void** factory);
    HRESULT WINAPI RoActivateInstance(HSTRING activatableClassId, IInspectable** instance);
    HRESULT WINAPI WindowsCreateString(LPCWSTR sourceString, UINT32 length, HSTRING* string);
    HRESULT WINAPI WindowsDeleteString(HSTRING string);
}
