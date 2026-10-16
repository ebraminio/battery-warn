/* toast.h - experimental WinRT toast with a bindable progress bar.
 *
 * The first call to toast_init() also registers the app's AUMID with
 * the shell by creating a Start Menu shortcut.  Without that, toasts
 * are accepted by the notification manager and then silently dropped.
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

bool toast_init();
void toast_shutdown();

bool toast_show_progress(const wchar_t *tag,
                         const wchar_t *title,
                         const wchar_t *body,
                         double value,             /* 0.0 .. 1.0 */
                         const wchar_t *value_string,
                         const wchar_t *status);

bool toast_update_progress(const wchar_t *tag,
                           double value,
                           const wchar_t *value_string,
                           const wchar_t *status);

bool toast_hide(const wchar_t *tag);

#ifdef __cplusplus
}
#endif
