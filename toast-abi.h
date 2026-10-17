#pragma once
#include <Windows.h>

typedef void *HSTRING;
typedef unsigned char boolean;

/* Move the enum to the top so we can use it in the interface definitions */
typedef enum NotificationUpdateResult {
    NotificationUpdateResult_Succeeded = 0,
    NotificationUpdateResult_Failed = 1,
    NotificationUpdateResult_NotFound = 2
} NotificationUpdateResult;

struct IInspectable {
    virtual HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppvObject) = 0;
    virtual ULONG STDMETHODCALLTYPE AddRef() = 0;
    virtual ULONG STDMETHODCALLTYPE Release() = 0;
    virtual HRESULT STDMETHODCALLTYPE GetIids(ULONG *iidCount, IID **iids) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetRuntimeClassName(HSTRING *className) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetTrustLevel(int *trustLevel) = 0;
};

struct IXmlDocument : IInspectable {};

struct IXmlDocumentIO : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE LoadXml(HSTRING xml) = 0;
};

/* 
 * The exact WinRT ABI vtable for IMap. 
 * It inherits directly from IInspectable, NOT IIterable.
 */
template <typename K, typename V>
struct IMap : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE Lookup(K key, V* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Size(UINT32* size) = 0;
    virtual HRESULT STDMETHODCALLTYPE HasKey(K key, boolean* found) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetView(void** view) = 0;
    virtual HRESULT STDMETHODCALLTYPE Insert(K key, V value, boolean* replaced) = 0;
    virtual HRESULT STDMETHODCALLTYPE Remove(K key) = 0;
    virtual HRESULT STDMETHODCALLTYPE Clear() = 0;
};

struct INotificationData : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_Values(IMap<HSTRING, HSTRING> **values) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_SequenceNumber(ULONG *value) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_SequenceNumber(ULONG value) = 0;
};

struct IToastNotification : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE add_Failed(void *handler, int *token) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_Failed(int token) = 0;
    virtual HRESULT STDMETHODCALLTYPE add_Dismissed(void *handler, int *token) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_Dismissed(int token) = 0;
    virtual HRESULT STDMETHODCALLTYPE add_Activated(void *handler, int *token) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_Activated(int token) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Content(IXmlDocument **content) = 0;
};

struct IToastNotification2 : IToastNotification {
    virtual HRESULT STDMETHODCALLTYPE put_Tag(HSTRING value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Tag(HSTRING *value) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_Group(HSTRING value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Group(HSTRING *value) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_SuppressPopup(boolean value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_SuppressPopup(boolean *value) = 0;
};

struct IToastNotification3 : IToastNotification2 {
    virtual HRESULT STDMETHODCALLTYPE put_NotificationMirroring(int value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_NotificationMirroring(int *value) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_RemoteId(HSTRING value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_RemoteId(HSTRING *value) = 0;
};

struct IToastNotification4 : IToastNotification3 {
    virtual HRESULT STDMETHODCALLTYPE get_Data(INotificationData **data) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_Data(INotificationData *data) = 0;
};

struct IToastNotifier : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE Show(IToastNotification *notification) = 0;
    virtual HRESULT STDMETHODCALLTYPE Hide(IToastNotification *notification) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Setting(int *value) = 0;
    virtual HRESULT STDMETHODCALLTYPE add_SettingChanged(void *handler, int *token) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_SettingChanged(int token) = 0;
};

struct IToastNotifier2 : IToastNotifier {
    virtual HRESULT STDMETHODCALLTYPE UpdateWithTag(INotificationData *data, HSTRING tag, NotificationUpdateResult *result) = 0;
};

struct IToastNotificationFactory : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE CreateToastNotification(IXmlDocument *content, IToastNotification **toast) = 0;
};

struct IToastNotificationManagerStatics : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE CreateToastNotifier(IToastNotifier **notifier) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateToastNotifierWithId(HSTRING applicationId, IToastNotifier **notifier) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_History(void **history) = 0;
};

/* Manually defined IIDs to replace __uuidof() */
static const IID kIID_IToastNotificationManagerStatics = {0x50AC103F, 0xD235, 0x4598, {0xBB, 0xEF, 0x98, 0xFE, 0x4D, 0x1A, 0x3A, 0xD4}};
static const IID kIID_IToastNotificationFactory = {0x04124B20, 0x82C6, 0x4229, {0xB1, 0x09, 0xFD, 0x9E, 0xD4, 0x66, 0x2B, 0x53}};
static const IID kIID_IXmlDocument = {0xF7F3A506, 0x1E87, 0x42D6, {0xBC, 0xFB, 0xB8, 0xC8, 0x09, 0xFA, 0x54, 0x94}};
static const IID kIID_IXmlDocumentIO = {0x6CD0E74E, 0xEE65, 0x4489, {0x9E, 0xBF, 0xCA, 0x43, 0xE8, 0x7B, 0xA6, 0x37}};
static const IID kIID_IToastNotification2 = {0x9DFB9FD1, 0x143A, 0x490E, {0x90, 0xBF, 0xB9, 0xFB, 0xA7, 0x13, 0x2D, 0xE7}};
static const IID kIID_IToastNotification4 = {0x15154935, 0x28EA, 0x4727, {0x88, 0xE9, 0xC5, 0x86, 0x80, 0xE2, 0xD1, 0x18}};
static const IID kIID_IToastNotifier2 = {0x354389C6, 0x7C01, 0x4BD5, {0x9C, 0x20, 0x60, 0x43, 0x4E, 0x35, 0x54, 0x9B}};
static const IID kIID_INotificationData = {0x9A9D0E44, 0x2BFD, 0x44B4, {0x80, 0x44, 0x9F, 0x6A, 0xE0, 0xB1, 0xDB, 0x47}};

extern "C" {
    HRESULT WINAPI RoInitialize(int initType);
    void WINAPI RoUninitialize();
    HRESULT WINAPI RoGetActivationFactory(HSTRING activatableClassId, REFIID iid, void **factory);
    HRESULT WINAPI RoActivateInstance(HSTRING activatableClassId, IInspectable **instance);
    HRESULT WINAPI WindowsCreateString(LPCWSTR sourceString, UINT32 length, HSTRING *string);
    HRESULT WINAPI WindowsDeleteString(HSTRING string);
}
