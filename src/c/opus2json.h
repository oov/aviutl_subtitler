#pragma once

#include <ovbase.h>

struct opus2json_context;

struct opus2json_params {
  wchar_t const *opus_path;
  wchar_t const *whisper_path;
  wchar_t const *additional_args;
  void *userdata;
  void (*on_progress)(void *const userdata, int const progress);
  void (*on_log_line)(void *const userdata, wchar_t const *const message);
  void (*on_finish)(void *const userdata, error err);
};

NODISCARD error opus2json_create(struct opus2json_context **const ctxpp, struct opus2json_params const *const params);
void opus2json_destroy(struct opus2json_context **const ctxpp);
NODISCARD error opus2json_abort(struct opus2json_context *const ctx);
