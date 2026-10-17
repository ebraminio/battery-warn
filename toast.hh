/* toast.hh - experimental WinRT toast with a bindable progress bar. */

#pragma once
#include <Windows.h>

struct Toast {
    Toast();
    ~Toast();

    Toast(const Toast &) = delete;
    Toast &operator=(const Toast &) = delete;

    bool init();
    void shutdown();

    bool show_progress(const wchar_t *tag,
                       const wchar_t *title,
                       const wchar_t *body,
                       double value,
                       const wchar_t *value_string,
                       const wchar_t *status);

    bool update_progress(const wchar_t *tag,
                         double value,
                         const wchar_t *value_string,
                         const wchar_t *status);

    bool hide(const wchar_t *tag);

private:
    struct HStr {
        void *h;
        Toast *state;
        HStr(Toast *state_, const wchar_t *s);
        ~HStr();
        operator void *() const { return h; }
        HStr(const HStr &) = delete;
        HStr &operator=(const HStr &) = delete;
    };

    bool fill_values(void *data, double value, const wchar_t *value_string,
                     const wchar_t *status, const wchar_t *title,
                     const wchar_t *body, bool include_title_and_body);

    void *notifier_;
    void *current_toast_;
    HMODULE mod_;

    HRESULT (WINAPI *pRoInitialize_)(int);
    void    (WINAPI *pRoUninitialize_)();
    HRESULT (WINAPI *pRoGetActivationFactory_)(void *, REFIID, void **);
    HRESULT (WINAPI *pRoActivateInstance_)(void *, void **);
    HRESULT (WINAPI *pWindowsCreateString_)(LPCWSTR, UINT32, void **);
    HRESULT (WINAPI *pWindowsDeleteString_)(void *);
};
