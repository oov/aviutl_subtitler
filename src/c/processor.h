#pragma once

#include <ovbase.h>

#include "aviutl.h"
#include "config.h"

struct processor_context;

enum processor_type {
  processor_type_invalid = 0,
  processor_type_raw2opus = 1,
  processor_type_opus2json = 2,
  processor_type_json2exo = 3,
};

struct processor_exo_info {
  wchar_t const *exo_path;
  int start_frame;
  int end_frame;
  int layer_min;
  int layer_max;
};

struct processor_params {
  HINSTANCE hinst;
  FILTER *fp;
  void *editp;

  void *userdata;
  void (*on_next_task)(void *const userdata, enum processor_type const type);
  void (*on_progress)(void *const userdata, int const progress);
  void (*on_log_line)(void *const userdata, wchar_t const *const message);
  void (*on_create_exo)(void *const userdata, struct processor_exo_info const *const info);
  void (*on_finish)(void *const userdata, error err);
};

struct processor_module {
  wchar_t const *module;
  wchar_t const *name;
  wchar_t const *description;
};

NODISCARD error processor_create(struct processor_context **const pp, struct processor_params const *const params);
void processor_destroy(struct processor_context **const pp);
struct config *processor_get_config(struct processor_context *const p);
NODISCARD error processor_run(struct processor_context *const p);
NODISCARD error processor_run_raw2opus(struct processor_context *const p);
NODISCARD error processor_run_opus2json(struct processor_context *const p);
NODISCARD error processor_run_json2exo(struct processor_context *const p);
NODISCARD error processor_abort(struct processor_context *const p);
void processor_clean(struct processor_context *const p);

NODISCARD error processor_get_modules(struct processor_context *const p, struct processor_module **const pmpp);
void processor_module_destroy(struct processor_module **const pmpp);
