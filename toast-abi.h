#pragma once
#include <Windows.h>

/* Suppress the pedantic COM warnings that Clang's -Weverything
   complains about (COM interfaces don't have virtual destructors). */
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wnon-virtual-dtor"
#pragma clang diagnostic ignored "-Wreserved-identifier"

/* HSTRING is just an opaque handle. Using void* avoids the reserved
   identifier warning that comes from typing HSTRING__. */
typedef void *HSTRING;
struct IInspectable;

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

struct IMapStringString : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE Lookup(HSTRING key, HSTRING *value) = 0;
    virtual HRESULT STDMETHODCALLTYPE HasKey(HSTRING key, unsigned char *found) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetView(void **view) = 0;
    virtual HRESULT STDMETHODCALLTYPE Insert(HSTRING key, HSTRING value, unsigned char *replaced) = 0;
    virtual HRESULT STDMETHODCALLTYPE Remove(HSTRING key) = 0;
    virtual HRESULT STDMETHODCALLTYPE Clear() = 0;
};

struct INotificationData : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_Values(IMapStringString **values) = 0;
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
    virtual HRESULT STDMETHODCALLTYPE put_SuppressPopup(unsigned char value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_SuppressPopup(unsigned char *value) = 0;
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
    virtual HRESULT STDMETHODCALLTYPE UpdateWithTag(INotificationData *data, HSTRING tag, int *result) = 0;
};

struct IToastNotificationFactory : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE CreateToastNotification(IXmlDocument *content, IToastNotification **toast) = 0;
};

struct IToastNotificationManagerStatics : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE CreateToastNotifier(IToastNotifier **notifier) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateToastNotifierWithId(HSTRING applicationId, IToastNotifier **notifier) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_History(void **history) = 0;
};

#pragma clang diagnostic pop

typedef enum NotificationUpdateResult {
    NotificationUpdateResult_Succeeded = 0,
    NotificationUpdateResult_Failed = 1,
    NotificationUpdateResult_NotFound = 2
} NotificationUpdateResult;

extern "C" {
    HRESULT WINAPI RoInitialize(int initType);
    void WINAPI RoUninitialize();
    HRESULT WINAPI RoGetActivationFactory(HSTRING activatableClassId, REFIID iid, void **factory);
    HRESULT WINAPI RoActivateInstance(HSTRING activatableClassId, IInspectable **instance);
    HRESULT WINAPI WindowsCreateString(LPCWSTR sourceString, UINT32 length, HSTRING *string);
    HRESULT WINAPI WindowsDeleteString(HSTRING string);
}
