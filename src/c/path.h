#pragma once

#include <ovbase.h>

#include <ovutil/win32.h>

NODISCARD error path_get_temp_file(wchar_t **const path, wchar_t const *const filename);
wchar_t const *path_extract_file_name_const(wchar_t const *const path);
wchar_t *path_extract_file_name_mut(wchar_t *const path);
#define path_extract_file_name(path)                                                                                   \
  _Generic((path), wchar_t const *: path_extract_file_name_const, wchar_t *: path_extract_file_name_mut)(path)

NODISCARD error path_select_file(HWND const window,
                                 wchar_t const *const title,
                                 wchar_t const *const filter,
                                 GUID const *const client_id,
                                 wchar_t **const path);
NODISCARD error path_select_folder(HWND const window,
                                   wchar_t const *const title,
                                   GUID const *const client_id,
                                   wchar_t **const path);
