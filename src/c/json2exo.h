#pragma once

#include <ovbase.h>

#include "aviutl.h"

struct json2exo_context;

struct json2exo_info {
  wchar_t const *exo_path;
  int length;
  int layer_min;
  int layer_max;
  int num_objects;
};

struct json2exo_params {
  FILTER *fp;
  void *editp;
  wchar_t const *json_path;
  wchar_t const *exo_path;
  wchar_t const *lua_directory;
  wchar_t const *module;
  void *userdata;
  void (*on_progress)(void *const userdata, int const progress);
  void (*on_log_line)(void *const userdata, wchar_t const *const message);
  void (*on_finish)(void *const userdata, struct json2exo_info const *const info, error err);
};

NODISCARD error json2exo_create(struct json2exo_context **const ctxpp, struct json2exo_params *params);
void json2exo_destroy(struct json2exo_context **const ctxpp);
NODISCARD error json2exo_abort(struct json2exo_context *const ctx);
