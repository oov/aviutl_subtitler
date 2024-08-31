#pragma once

#include <ovbase.h>

struct raw2opus_context;

struct raw2opus_info {
  int sample_rate;
  int channels;
  size_t samples;
};

struct raw2opus_params {
  void *fp;
  void *editp;
  wchar_t const *opus_path;
  void *userdata;
  void (*on_progress)(void *const userdata, int const progress);
  void (*on_log_line)(void *const userdata, wchar_t const *const message);
  void (*on_finish)(void *const userdata, struct raw2opus_info const *const info, error err);
};

NODISCARD error raw2opus_create(struct raw2opus_context **const ctxpp, struct raw2opus_params const *const params);
void raw2opus_destroy(struct raw2opus_context **const ctxpp);
NODISCARD error raw2opus_abort(struct raw2opus_context *const ctx);
