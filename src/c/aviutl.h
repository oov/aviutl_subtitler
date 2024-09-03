#pragma once

#include <stdbool.h>
#include <stdint.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#ifdef __GNUC__
#  ifndef __has_warning
#    define __has_warning(x) 0
#  endif
#  pragma GCC diagnostic push
#  if __has_warning("-Winvalid-utf8")
#    pragma GCC diagnostic ignored "-Winvalid-utf8"
#  endif
#endif // __GNUC__

#include "3rd/aviutl_sdk/filter.h"

#ifdef __GNUC__
#  pragma GCC diagnostic pop
#endif // __GNUC__

#include <ovbase.h>

enum aviutl_patched {
  aviutl_patched_default = 0,
  aviutl_patched_en = 1,
  aviutl_patched_zh_cn = 2,
};

void aviutl_set_pointers(FILTER *const fp, void *const editp);
void aviutl_get_pointers(FILTER **const fp, void **const editp);
NODISCARD error aviutl_init(void);
NODISCARD bool aviutl_initalized(void);
NODISCARD error aviutl_exit(void);
NODISCARD error aviutl_get_patch(enum aviutl_patched *const patched);
NODISCARD HWND aviutl_get_my_window(void);

NODISCARD error aviutl_drop_exo(char const *const exo_path, int frame, int layer, int frames);
NODISCARD error aviutl_find_space(int const required_spaces,
                                  int const search_offset,
                                  bool const last,
                                  int *const found_target);
