#pragma once

#define WIN32_LEAN_AND_MEAN
#define UNICODE
#ifndef WINVER
#define WINVER 0x0601
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#include <Windows.h>

#if defined(__has_cpp_attribute)
#if __has_cpp_attribute(clang::lifetimebound)
#define LIFETIME_BOUND [[clang::lifetimebound]]
#else
#define LIFETIME_BOUND
#endif
#else
#define LIFETIME_BOUND
#endif

class Toast {
public:
    Toast();
    ~Toast();

    Toast(const Toast &) = delete;
    Toast &operator=(const Toast &) = delete;

    bool init();
    void shutdown();

    bool show_progress(const wchar_t *tag,
                       const wchar_t *title,
                       const wchar_t *body,
                       unsigned int value_num,
                       unsigned int value_den,
                       const wchar_t *value_string,
                       const wchar_t *status);

    bool update_progress(const wchar_t *tag,
                         unsigned int value_num,
                         unsigned int value_den,
                         const wchar_t *value_string,
                         const wchar_t *status);

    bool hide(const wchar_t *tag);

private:
    struct HStr {
        void *h;
        Toast *state;
        HStr(Toast *state_ LIFETIME_BOUND, const wchar_t *s);
        ~HStr();
        operator void *() const { return h; }
        HStr(const HStr &) = delete;
        HStr &operator=(const HStr &) = delete;
    };

    bool fill_values(void *data, unsigned int value_num, unsigned int value_den,
                     const wchar_t *value_string, const wchar_t *status,
                     const wchar_t *title, const wchar_t *body,
                     bool include_title_and_body);

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