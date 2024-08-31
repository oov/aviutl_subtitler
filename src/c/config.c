#include "config.h"

#include <ovarray.h>

#include "i18n.h"
#include "jsoncommon.h"

static NODISCARD error wide_to_utf8(wchar_t const *const src, char **const dest) {
  if (!src || !dest) {
    return errg(err_invalid_arugment);
  }
  error err = eok();
  size_t const slen = wcslen(src);
  int const dlen = slen == 0 ? 0 : WideCharToMultiByte(CP_UTF8, 0, src, (int)slen, NULL, 0, NULL, NULL);
  err = OV_ARRAY_GROW(dest, (size_t)(dlen + 1));
  if (efailed(err)) {
    err = ethru(err);
    goto cleanup;
  }
  if (slen != 0) {
    if (WideCharToMultiByte(CP_UTF8, 0, src, (int)slen, *dest, dlen, NULL, NULL) == 0) {
      err = errhr(HRESULT_FROM_WIN32(GetLastError()));
      goto cleanup;
    }
  }
  (*dest)[dlen] = '\0';
cleanup:
  return err;
}

static NODISCARD error utf8_to_wide(char const *const src, wchar_t **const dest) {
  if (!src || !dest) {
    return errg(err_invalid_arugment);
  }
  error err = eok();
  size_t const slen = strlen(src);
  int const dlen = slen == 0 ? 0 : MultiByteToWideChar(CP_UTF8, 0, src, (int)slen, NULL, 0);
  err = OV_ARRAY_GROW(dest, (size_t)(dlen + 1));
  if (efailed(err)) {
    err = ethru(err);
    goto cleanup;
  }
  if (slen != 0) {
    if (MultiByteToWideChar(CP_UTF8, 0, src, (int)slen, *dest, dlen) == 0) {
      err = errhr(HRESULT_FROM_WIN32(GetLastError()));
      goto cleanup;
    }
  }
  (*dest)[dlen] = L'\0';
cleanup:
  return err;
}

struct config {
  wchar_t *whisper_path;
  wchar_t *model;
  wchar_t *language;
  wchar_t *module;
  wchar_t *initial_prompt;
  wchar_t *model_dir;
  wchar_t *additional_args;
  int insert_position;
  int insert_mode;
};

NODISCARD error config_create(struct config **const cfgpp) {
  if (!cfgpp || *cfgpp) {
    return errg(err_invalid_arugment);
  }
  struct config *cfg = NULL;
  error err = mem(&cfg, 1, sizeof(struct config));
  if (efailed(err)) {
    err = ethru(err);
    goto cleanup;
  }
  *cfg = (struct config){0};
  *cfgpp = cfg;
  cfg = NULL;
cleanup:
  if (cfg) {
    config_destroy(&cfg);
  }
  return err;
}

void config_destroy(struct config **const cfgpp) {
  if (!cfgpp || !*cfgpp) {
    return;
  }
  struct config *cfg = *cfgpp;
  OV_ARRAY_DESTROY(&cfg->whisper_path);
  OV_ARRAY_DESTROY(&cfg->model);
  OV_ARRAY_DESTROY(&cfg->language);
  OV_ARRAY_DESTROY(&cfg->module);
  OV_ARRAY_DESTROY(&cfg->initial_prompt);
  OV_ARRAY_DESTROY(&cfg->model_dir);
  OV_ARRAY_DESTROY(&cfg->additional_args);
  ereport(mem_free(cfgpp));
}

void config_reset(struct config *const cfg) {
  if (!cfg) {
    return;
  }
#define DEFINE_RESET_STRING_PROPERTY(NAME)                                                                             \
  if (cfg->NAME) {                                                                                                     \
    cfg->NAME[0] = L'\0';                                                                                              \
  }
#define DEFINE_RESET_INT_PROPERTY(NAME) cfg->NAME = 0;
  DEFINE_RESET_STRING_PROPERTY(whisper_path)
  DEFINE_RESET_STRING_PROPERTY(model)
  DEFINE_RESET_STRING_PROPERTY(language)
  DEFINE_RESET_STRING_PROPERTY(module)
  DEFINE_RESET_STRING_PROPERTY(initial_prompt)
  DEFINE_RESET_STRING_PROPERTY(model_dir)
  DEFINE_RESET_STRING_PROPERTY(additional_args)
  DEFINE_RESET_INT_PROPERTY(insert_position)
  DEFINE_RESET_INT_PROPERTY(insert_mode)
#undef DEFINE_RESET_STRING_PROPERTY
#undef DEFINE_RESET_INT_PROPERTY
}

NODISCARD error config_load(struct config *const cfg, char const *const buf, size_t const buflen) {
  if (!cfg || !buf) {
    return errg(err_invalid_arugment);
  }
  error err = eok();
  wchar_t *s = NULL;
  struct yyjson_doc *doc = NULL;

  struct yyjson_read_err read_err;
  doc = yyjson_read_opts(ov_deconster_(buf), buflen, 0, jsoncommon_get_json_alc(), &read_err);
  if (!doc) {
    err = emsg_i18nf(err_type_generic,
                     err_fail,
                     L"%1$hs%2$d",
                     gettext("Unable to parse JSON: %1$hs (line: %2$d)"),
                     read_err.msg,
                     read_err.pos);
    goto cleanup;
  }

  struct yyjson_val *root = yyjson_doc_get_root(doc);
  if (!root || !yyjson_is_obj(root)) {
    err = emsg_i18n(err_type_generic, err_fail, gettext("The root of the JSON must be an object."));
    goto cleanup;
  }

#define GET_STRING_PROPERTY(NAME)                                                                                      \
  {                                                                                                                    \
    struct yyjson_val *const v = yyjson_obj_get(root, #NAME);                                                          \
    if (v && yyjson_is_str(v)) {                                                                                       \
      err = utf8_to_wide(yyjson_get_str(v), &s);                                                                       \
      if (efailed(err)) {                                                                                              \
        err = ethru(err);                                                                                              \
        goto cleanup;                                                                                                  \
      }                                                                                                                \
      err = config_set_##NAME(cfg, s);                                                                                 \
      if (efailed(err)) {                                                                                              \
        err = ethru(err);                                                                                              \
        goto cleanup;                                                                                                  \
      }                                                                                                                \
    }                                                                                                                  \
  }
#define GET_INT_PROPERTY(NAME)                                                                                         \
  {                                                                                                                    \
    struct yyjson_val *const v = yyjson_obj_get(root, #NAME);                                                          \
    if (v && yyjson_is_int(v)) {                                                                                       \
      int const i = yyjson_get_int(v);                                                                                 \
      err = config_set_##NAME(cfg, i);                                                                                 \
      if (efailed(err)) {                                                                                              \
        err = ethru(err);                                                                                              \
        goto cleanup;                                                                                                  \
      }                                                                                                                \
    }                                                                                                                  \
  }
  GET_STRING_PROPERTY(whisper_path)
  GET_STRING_PROPERTY(model)
  GET_STRING_PROPERTY(language)
  GET_STRING_PROPERTY(module)
  GET_STRING_PROPERTY(initial_prompt)
  GET_STRING_PROPERTY(model_dir)
  GET_STRING_PROPERTY(additional_args)
  GET_INT_PROPERTY(insert_position)
  GET_INT_PROPERTY(insert_mode)
#undef GET_STRING_PROPERTY
#undef GET_INT_PROPERTY

cleanup:
  OV_ARRAY_DESTROY(&s);
  if (doc) {
    yyjson_doc_free(doc);
    doc = NULL;
  }
  return err;
}

NODISCARD error config_load_file(struct config *const cfg, wchar_t const *const path) {
  if (!cfg || !path) {
    return errg(err_invalid_arugment);
  }
  error err = eok();
  HANDLE h = INVALID_HANDLE_VALUE;
  char *json = NULL;
  h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
  if (h == INVALID_HANDLE_VALUE) {
    err = errhr(HRESULT_FROM_WIN32(GetLastError()));
    goto cleanup;
  }
  LARGE_INTEGER size;
  if (!GetFileSizeEx(h, &size)) {
    err = errhr(HRESULT_FROM_WIN32(GetLastError()));
    goto cleanup;
  }
  if (size.QuadPart > INT_MAX) {
    err = emsg_i18n(err_type_generic, err_fail, gettext("The file is too large."));
    goto cleanup;
  }
  DWORD read;
  err = mem(&json, (size_t)size.QuadPart + 1, sizeof(char));
  if (efailed(err)) {
    err = ethru(err);
    goto cleanup;
  }
  if (!ReadFile(h, json, (DWORD)size.QuadPart, &read, NULL)) {
    err = errhr(HRESULT_FROM_WIN32(GetLastError()));
    goto cleanup;
  }
  if (read != (DWORD)size.QuadPart) {
    err = emsg_i18n(err_type_generic, err_fail, gettext("Unable to read the entire file."));
    goto cleanup;
  }
  json[read] = '\0';
  err = config_load(cfg, json, (size_t)read);
  if (efailed(err)) {
    err = ethru(err);
    goto cleanup;
  }
cleanup:
  if (h != INVALID_HANDLE_VALUE) {
    CloseHandle(h);
    h = INVALID_HANDLE_VALUE;
  }
  if (json) {
    ereport(mem_free(&json));
  }
  return err;
}

NODISCARD error config_save(struct config *const cfg, char **const buf, size_t *const buflen) {
  if (!cfg || !buf || *buf) {
    return errg(err_invalid_arugment);
  }
  error err = eok();
  char *s = NULL;
  struct yyjson_mut_doc *doc = NULL;
  doc = yyjson_mut_doc_new(jsoncommon_get_json_alc());
  if (!doc) {
    err = errg(err_out_of_memory);
    goto cleanup;
  }
  struct yyjson_mut_val *root = yyjson_mut_obj(doc);
  if (!root) {
    err = errg(err_fail);
    goto cleanup;
  }
  yyjson_mut_doc_set_root(doc, root);

#define ADD_STRING_PROPERTY(NAME)                                                                                      \
  err = wide_to_utf8(config_get_##NAME(cfg), &s);                                                                      \
  if (efailed(err)) {                                                                                                  \
    err = ethru(err);                                                                                                  \
    goto cleanup;                                                                                                      \
  }                                                                                                                    \
  yyjson_mut_obj_add_strcpy(doc, root, #NAME, s);
#define ADD_INT_PROPERTY(NAME) yyjson_mut_obj_add_int(doc, root, #NAME, config_get_##NAME(cfg));

  ADD_STRING_PROPERTY(whisper_path)
  ADD_STRING_PROPERTY(model)
  ADD_STRING_PROPERTY(language)
  ADD_STRING_PROPERTY(module)
  ADD_STRING_PROPERTY(initial_prompt)
  ADD_STRING_PROPERTY(model_dir)
  ADD_STRING_PROPERTY(additional_args)
  ADD_INT_PROPERTY(insert_position)
  ADD_INT_PROPERTY(insert_mode)
#undef ADD_STRING_PROPERTY
#undef ADD_INT_PROPERTY

  struct yyjson_write_err jsonerr;
  *buf = yyjson_mut_write_opts(doc, 0, jsoncommon_get_json_alc(), buflen, &jsonerr);
  if (!*buf) {
    err = emsg_i18nf(err_type_generic, err_fail, L"%1$hs%2$d", gettext("Unable to write JSON: %1$hs"), jsonerr.msg);
    goto cleanup;
  }

cleanup:
  OV_ARRAY_DESTROY(&s);
  if (doc) {
    yyjson_mut_doc_free(doc);
    doc = NULL;
  }
  return err;
}

NODISCARD error config_save_file(struct config *const cfg, wchar_t const *const path) {
  if (!cfg || !path) {
    return errg(err_invalid_arugment);
  }
  size_t jsonlen = 0;
  char *json = NULL;
  HANDLE h = INVALID_HANDLE_VALUE;

  error err = config_save(cfg, &json, &jsonlen);
  if (efailed(err)) {
    err = ethru(err);
    goto cleanup;
  }
  h = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
  if (h == INVALID_HANDLE_VALUE) {
    err = errhr(HRESULT_FROM_WIN32(GetLastError()));
    goto cleanup;
  }
  DWORD written;
  if (!WriteFile(h, json, (DWORD)jsonlen, &written, NULL)) {
    err = errhr(HRESULT_FROM_WIN32(GetLastError()));
    goto cleanup;
  }
  if (written != jsonlen) {
    err = emsg_i18n(err_type_generic, err_fail, gettext("Unable to write the entire file."));
    goto cleanup;
  }
cleanup:
  if (h != INVALID_HANDLE_VALUE) {
    CloseHandle(h);
    h = INVALID_HANDLE_VALUE;
    if (efailed(err)) {
      DeleteFileW(path);
    }
  }
  if (json) {
    ereport(mem_free(&json));
  }
  return err;
}

NODISCARD error config_verify_whisper_path(struct config *const cfg) {
  if (!cfg) {
    return errg(err_invalid_arugment);
  }
  error err = eok();
  wchar_t const *const whisper_path = config_get_whisper_path(cfg);
  if (!whisper_path) {
    err = emsg_i18nf(
        err_type_generic, err_fail, L"%1$hs", gettext("The path to %1$hs executable is not set."), gettext("Whisper"));
    goto cleanup;
  }
  if (GetFileAttributesW(whisper_path) == INVALID_FILE_ATTRIBUTES) {
    err = emsg_i18nf(err_type_generic, err_fail, L"%1$hs", "%1$hs", gettext("File is not found."));
    goto cleanup;
  }
cleanup:
  return err;
}

static NODISCARD error set_string(wchar_t **dest, wchar_t const *const def, wchar_t const *const v) {
  if (!dest || !v) {
    return errg(err_invalid_arugment);
  }
  error err = eok();
  if (*dest == def) {
    if (wcscmp(v, def) == 0) {
      goto cleanup;
    }
    *dest = NULL;
  }
  err = OV_ARRAY_GROW(dest, wcslen(v) + 1);
  if (efailed(err)) {
    err = ethru(err);
    goto cleanup;
  }
  wcscpy(*dest, v);
cleanup:
  return err;
}

#define DEFINE_STRING_PROPERTY(NAME, DEFAULT)                                                                          \
  NODISCARD error config_set_##NAME(struct config *const cfg, wchar_t const *const v) {                                \
    if (!cfg) {                                                                                                        \
      return errg(err_invalid_arugment);                                                                               \
    }                                                                                                                  \
    return set_string(&cfg->NAME, config_get_##NAME##_default(), v);                                                   \
  }                                                                                                                    \
                                                                                                                       \
  wchar_t const *config_get_##NAME(struct config const *const cfg) {                                                   \
    if (!cfg) {                                                                                                        \
      return NULL;                                                                                                     \
    }                                                                                                                  \
    if (!cfg->NAME) {                                                                                                  \
      return config_get_##NAME##_default();                                                                            \
    }                                                                                                                  \
    return cfg->NAME;                                                                                                  \
  }                                                                                                                    \
                                                                                                                       \
  wchar_t const *config_get_##NAME##_default(void) {                                                                   \
    static wchar_t const def[] = DEFAULT;                                                                              \
    return def;                                                                                                        \
  }
#define DEFINE_INT_PROPERTY(NAME, DEFAULT)                                                                             \
  NODISCARD error config_set_##NAME(struct config *const cfg, int const v) {                                           \
    if (!cfg) {                                                                                                        \
      return errg(err_invalid_arugment);                                                                               \
    }                                                                                                                  \
    cfg->NAME = v;                                                                                                     \
    return eok();                                                                                                      \
  }                                                                                                                    \
  int config_get_##NAME(struct config const *const cfg) {                                                              \
    if (!cfg) {                                                                                                        \
      return 0;                                                                                                        \
    }                                                                                                                  \
    return cfg->NAME;                                                                                                  \
  }                                                                                                                    \
  int config_get_##NAME##_default(void) { return DEFAULT; }

DEFINE_STRING_PROPERTY(whisper_path, L"")
DEFINE_STRING_PROPERTY(model, L"large-v3")
DEFINE_STRING_PROPERTY(language, L"")
DEFINE_STRING_PROPERTY(module, L"text")
DEFINE_STRING_PROPERTY(initial_prompt, L"")
DEFINE_STRING_PROPERTY(model_dir, L"")
DEFINE_STRING_PROPERTY(additional_args, L"")
DEFINE_INT_PROPERTY(insert_position, 1)
DEFINE_INT_PROPERTY(insert_mode, 1)
#undef DEFINE_STRING_PROPERTY
#undef DEFINE_INT_PROPERTY
