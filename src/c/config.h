#pragma once

#include <ovbase.h>

struct config;

NODISCARD error config_create(struct config **const cfgpp);
void config_destroy(struct config **const cfgpp);
void config_reset(struct config *const cfg);
NODISCARD error config_load(struct config *const cfg, char const *const buf, size_t const buflen);
NODISCARD error config_save(struct config *const cfg, char **const buf, size_t *const buflen);
NODISCARD error config_load_file(struct config *const cfg, wchar_t const *const path);
NODISCARD error config_save_file(struct config *const cfg, wchar_t const *const path);
NODISCARD error config_verify_whisper_path(struct config *const cfg);

#define DEFINE_STRING_PROPERTY(NAME)                                                                                   \
  NODISCARD error config_set_##NAME(struct config *const cfg, wchar_t const *const v);                                 \
  wchar_t const *config_get_##NAME(struct config const *const cfg);                                                    \
  wchar_t const *config_get_##NAME##_default(void);

#define DEFINE_INT_PROPERTY(NAME)                                                                                      \
  NODISCARD error config_set_##NAME(struct config *const cfg, int const v);                                            \
  int config_get_##NAME(struct config const *const cfg);                                                               \
  int config_get_##NAME##_default(void);

DEFINE_STRING_PROPERTY(whisper_path)
DEFINE_STRING_PROPERTY(model)
DEFINE_STRING_PROPERTY(language)
DEFINE_STRING_PROPERTY(module)
DEFINE_STRING_PROPERTY(initial_prompt)
DEFINE_STRING_PROPERTY(model_dir)
DEFINE_STRING_PROPERTY(additional_args)
DEFINE_INT_PROPERTY(insert_position)
DEFINE_INT_PROPERTY(insert_mode)
#undef DEFINE_STRING_PROPERTY
#undef DEFINE_INT_PROPERTY
