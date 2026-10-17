/* toast.h - experimental WinRT toast with a bindable progress bar. */

#pragma once
#include <Windows.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Define HSTRING as a void pointer so the header is self-contained */
typedef void *HSTRING;

typedef struct toast_state_t {
    void *notifier;
    void *current_toast;

    /* combase.dll function pointers */
    HMODULE mod;
    HRESULT (WINAPI *RoInitialize)(int);
    void    (WINAPI *RoUninitialize)();
    HRESULT (WINAPI *RoGetActivationFactory)(HSTRING, REFIID, void **);
    HRESULT (WINAPI *RoActivateInstance)(HSTRING, void **);
    HRESULT (WINAPI *WindowsCreateString)(LPCWSTR, UINT32, HSTRING *);
    HRESULT (WINAPI *WindowsDeleteString)(HSTRING);
} toast_state_t;

bool toast_init(toast_state_t *state);
void toast_shutdown(toast_state_t *state);

bool toast_show_progress(toast_state_t *state,
                         const wchar_t *tag,
                         const wchar_t *title,
                         const wchar_t *body,
                         double value,             /* 0.0 .. 1.0 */
                         const wchar_t *value_string,
                         const wchar_t *status);

bool toast_update_progress(toast_state_t *state,
                           const wchar_t *tag,
                           double value,
                           const wchar_t *value_string,
                           const wchar_t *status);

bool toast_hide(toast_state_t *state, const wchar_t *tag);

#ifdef __cplusplus
}
#endif
