#include "processor.h"

#include <ovarray.h>
#include <ovthreads.h>

#include "config.h"
#include "i18n.h"
#include "json2exo.h"
#include "luactx.h"
#include "opus2json.h"
#include "path.h"
#include "raw2opus.h"

struct processor {
  struct config *config;
  struct processor_params params;
  thrd_t thread;
  enum processor_type type;
  int progress;
  bool aborted;
};

static NODISCARD error get_json_path(wchar_t **const json_path, HINSTANCE const hinst) {
  error err = path_get_module_name(json_path, hinst);
  if (efailed(err)) {
    err = ethru(err);
    goto cleanup;
  }
  err = OV_ARRAY_GROW(json_path, wcslen(*json_path) + 6);
  wchar_t *ext = wcsrchr(*json_path, L'.');
  if (!ext) {
    ext = *json_path + wcslen(*json_path);
  }
  wcscpy(ext, L".json");
cleanup:
  return err;
}

static NODISCARD error get_lua_directory(wchar_t **const lua_path, HINSTANCE const hinst) {
  static wchar_t const directory[] = L"Subtitler";
  error err = path_get_module_name(lua_path, hinst);
  if (efailed(err)) {
    err = ethru(err);
    goto cleanup;
  }
  wchar_t *delim = wcsrchr(*lua_path, L'\\');
  if (!delim) {
    err = emsg_i18n(err_type_generic, err_fail, gettext("Unexpected path for the module file."));
    goto cleanup;
  }
  err = OV_ARRAY_GROW(lua_path, (size_t)(delim - *lua_path) + 1 + wcslen(directory));
  if (efailed(err)) {
    err = ethru(err);
    goto cleanup;
  }
  wcscpy(delim + 1, directory);
cleanup:
  return err;
}

static NODISCARD error get_target_file_path(wchar_t **const path,
                                            HINSTANCE const hinst,
                                            bool const solo,
                                            wchar_t const *const ext) {
  wchar_t *module_name = NULL;
  error err = path_get_module_name(&module_name, hinst);
  if (efailed(err)) {
    err = ethru(err);
    goto cleanup;
  }
  err = OV_ARRAY_GROW(&module_name, OV_ARRAY_LENGTH(module_name) + 64);
  if (efailed(err)) {
    err = ethru(err);
    goto cleanup;
  }
  wchar_t *delim = wcsrchr(module_name, L'\\');
  wchar_t *e = wcsrchr(module_name, L'.');
  if (!delim || !e) {
    err = emsg_i18n(err_type_generic, err_fail, gettext("Unexpected path for the module file."));
    goto cleanup;
  }
  *e = L'\0';
  wchar_t *real_module_name = delim + 1;
  if (solo) {
    wcscpy(e, ext);
  } else {
    wsprintfW(e, L"_%d%s", GetCurrentProcessId(), ext);
  }
  err = path_get_temp_file(path, real_module_name);
  if (efailed(err)) {
    err = ethru(err);
    goto cleanup;
  }
cleanup:
  OV_ARRAY_DESTROY(&module_name);
  return err;
}

static NODISCARD error remove_temporary_files(HINSTANCE const hinst) {
  wchar_t *path = NULL;
  error err = get_target_file_path(&path, hinst, false, L".opus");
  if (efailed(err)) {
    err = ethru(err);
    goto cleanup;
  }
  DeleteFileW(path);
  wchar_t *ext = wcsrchr(path, L'.');
  wcscpy(ext, L".json");
  DeleteFileW(path);
  wcscpy(ext, L".exo");
  DeleteFileW(path);
cleanup:
  OV_ARRAY_DESTROY(&path);
  return err;
}

NODISCARD error processor_create(struct processor **const pp, struct processor_params const *const params) {
  if (!pp || *pp || !params) {
    return errg(err_invalid_arugment);
  }
  struct processor *p = NULL;
  wchar_t *json_path = NULL;
  error err = mem(&p, 1, sizeof(struct processor));
  if (efailed(err)) {
    err = ethru(err);
    goto cleanup;
  }
  *p = (struct processor){
      .params = *params,
  };
  err = config_create(&p->config);
  if (efailed(err)) {
    err = ethru(err);
    goto cleanup;
  }
  err = get_json_path(&json_path, params->hinst);
  if (efailed(err)) {
    err = ethru(err);
    goto cleanup;
  }
  err = config_load_file(p->config, json_path);
  if (efailed(err)) {
    if (eis_hr(err, HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND))) {
      efree(&err);
    } else {
      err = ethru(err);
      goto cleanup;
    }
  }
  *pp = p;
  p = NULL;
cleanup:
  if (json_path) {
    OV_ARRAY_DESTROY(&json_path);
  }
  if (p) {
    processor_destroy(&p);
  }
  return err;
}

void processor_destroy(struct processor **const pp) {
  if (!pp || !*pp) {
    return;
  }
  struct processor *p = *pp;
  wchar_t *json_path = NULL;
  error err = get_json_path(&json_path, p->params.hinst);
  if (efailed(err)) {
    err = ethru(err);
    goto cleanup;
  }
  err = config_save_file(p->config, json_path);
  if (efailed(err)) {
    err = ethru(err);
    goto cleanup;
  }
cleanup:
  if (p->config) {
    config_destroy(&p->config);
  }
  p->params.fp = NULL;
  p->params.editp = NULL;

  p->params.userdata = NULL;
  p->params.on_progress = NULL;
  p->params.on_log_line = NULL;
  p->params.on_finish = NULL;
  OV_ARRAY_DESTROY(&json_path);
  ereport(mem_free(pp));
  ereport(err);
}

struct config *processor_get_config(struct processor *const p) {
  if (!p) {
    return NULL;
  }
  return p->config;
}

static bool on_progress(void *const userdata, int const progress) {
  struct processor *const p = userdata;
  if (p->params.on_progress && !p->aborted && p->progress != progress) {
    p->progress = progress;
    p->params.on_progress(p->params.userdata, p->type, progress);
  }
  return !p->aborted;
}

static void on_log_line(void *const userdata, wchar_t const *const message) {
  struct processor const *const p = userdata;
  p->params.on_log_line(p->params.userdata, p->type, message);
}

static bool run_raw2opus(struct processor *const p, bool const solo) {
  wchar_t *opus_path = NULL;
  error err = eok();
  if (!p) {
    err = errg(err_invalid_arugment);
    goto cleanup;
  }
  err = config_verify_whisper_path(p->config);
  if (efailed(err)) {
    err = ethru(err);
    goto cleanup;
  }
  err = get_target_file_path(&opus_path, p->params.hinst, solo, L".opus");
  if (efailed(err)) {
    err = ethru(err);
    goto cleanup;
  }
  p->type = processor_type_raw2opus;
  p->progress = 0;
  if (p->params.on_start) {
    p->params.on_start(p->params.userdata, p->type);
  }
  struct raw2opus_info info;
  err = raw2opus(
      &(struct raw2opus_params){
          .fp = p->params.fp,
          .editp = p->params.editp,
          .opus_path = opus_path,
          .userdata = p,
          .on_progress = on_progress,
          .on_log_line = on_log_line,
      },
      &info);
cleanup:
  OV_ARRAY_DESTROY(&opus_path);
  bool const r = esucceeded(err);
  if (p->params.on_finish) {
    p->params.on_finish(p->params.userdata, p->type, err);
  } else {
    ereport(err);
  }
  return r;
}

static NODISCARD error escape(wchar_t const *const value, wchar_t **const buf) {
  error err = eok();
  size_t const len = wcslen(value);
  err = OV_ARRAY_GROW(buf, len * 2 + 1);
  if (efailed(err)) {
    err = ethru(err);
    goto cleanup;
  }
  wchar_t *p = *buf;
  for (size_t i = 0; i < len; ++i) {
    if (value[i] == L'\\' || value[i] == L'"') {
      *p++ = L'\\';
    }
    *p++ = value[i];
  }
  *p = L'\0';
cleanup:
  return err;
}

static NODISCARD error append_arg(wchar_t **const args,
                                  wchar_t **const buf,
                                  wchar_t const *const arg_template,
                                  wchar_t const *const value,
                                  bool const escape_value) {
  error err = eok();
  if (escape_value) {
    err = escape(value, buf);
    if (efailed(err)) {
      err = ethru(err);
      goto cleanup;
    }
    err = OV_ARRAY_GROW(args, OV_ARRAY_LENGTH(*args) + wcslen(*buf) + wcslen(arg_template) + 1);
    if (efailed(err)) {
      err = ethru(err);
      goto cleanup;
    }
    int n = wsprintfW(*args + OV_ARRAY_LENGTH(*args), arg_template, *buf);
    OV_ARRAY_SET_LENGTH(*args, OV_ARRAY_LENGTH(*args) + (size_t)n);
  } else {
    err = OV_ARRAY_GROW(args, OV_ARRAY_LENGTH(*args) + wcslen(value) + wcslen(arg_template) + 1);
    if (efailed(err)) {
      err = ethru(err);
      goto cleanup;
    }
    int n = wsprintfW(*args + OV_ARRAY_LENGTH(*args), arg_template, value);
    OV_ARRAY_SET_LENGTH(*args, OV_ARRAY_LENGTH(*args) + (size_t)n);
  }
cleanup:
  return err;
}

static bool run_opus2json(struct processor *const p, bool const solo) {
  wchar_t const *const whisper_path = config_get_whisper_path(p->config);
  wchar_t *opus_path = NULL;
  wchar_t *args = NULL;
  wchar_t *buf = NULL;
  error err = eok();
  if (!p) {
    err = errg(err_invalid_arugment);
    goto cleanup;
  }
  err = config_verify_whisper_path(p->config);
  if (efailed(err)) {
    err = ethru(err);
    goto cleanup;
  }
  err = get_target_file_path(&opus_path, p->params.hinst, solo, L".opus");
  if (efailed(err)) {
    err = ethru(err);
    goto cleanup;
  }

#define APPEND_CONFIG_ARG(CONFIG_FUNC, ARG_TEMPLATE, ESCAPE)                                                           \
  {                                                                                                                    \
    wchar_t const *const value = CONFIG_FUNC(p->config);                                                               \
    if (value && *value) {                                                                                             \
      err = append_arg(&args, &buf, ARG_TEMPLATE, value, ESCAPE);                                                      \
      if (efailed(err)) {                                                                                              \
        goto cleanup;                                                                                                  \
      }                                                                                                                \
    }                                                                                                                  \
  }
  APPEND_CONFIG_ARG(config_get_model, L"--model \"%s\" ", true)
  APPEND_CONFIG_ARG(config_get_language, L"--language \"%s\" ", true)
  APPEND_CONFIG_ARG(config_get_initial_prompt, L"--initial_prompt \"%s\" ", true)
  APPEND_CONFIG_ARG(config_get_model_dir, L"--model_dir \"%s\" ", true)
  APPEND_CONFIG_ARG(config_get_additional_args, L"%s ", false)
#undef APPEND_CONFIG_ARG

  p->type = processor_type_opus2json;
  p->progress = 0;
  if (p->params.on_start) {
    p->params.on_start(p->params.userdata, p->type);
  }

  err = opus2json(&(struct opus2json_params){
      .opus_path = opus_path,
      .whisper_path = whisper_path,
      .additional_args = args,
      .userdata = p,
      .on_progress = on_progress,
      .on_log_line = on_log_line,
  });
cleanup:
  OV_ARRAY_DESTROY(&buf);
  OV_ARRAY_DESTROY(&args);
  OV_ARRAY_DESTROY(&opus_path);
  bool const r = esucceeded(err);
  if (p->params.on_finish) {
    p->params.on_finish(p->params.userdata, p->type, err);
  } else {
    ereport(err);
  }
  return r;
}

static bool run_json2exo(struct processor *const p, bool const solo) {
  wchar_t *json_path = NULL;
  wchar_t *exo_path = NULL;
  wchar_t *lua_directory = NULL;
  error err = eok();
  if (!p) {
    err = errg(err_invalid_arugment);
    goto cleanup;
  }
  err = get_target_file_path(&json_path, p->params.hinst, solo, L".json");
  if (efailed(err)) {
    err = ethru(err);
    goto cleanup;
  }
  err = get_target_file_path(&exo_path, p->params.hinst, solo, L".exo");
  if (efailed(err)) {
    err = ethru(err);
    goto cleanup;
  }
  err = get_lua_directory(&lua_directory, p->params.hinst);
  if (efailed(err)) {
    err = ethru(err);
    goto cleanup;
  }
  wchar_t const *const module = config_get_module(p->config);
  if (!module || !*module) {
    err = emsg_i18n(err_type_generic, err_fail, gettext("The module is not set."));
    goto cleanup;
  }

  p->type = processor_type_json2exo;
  p->progress = 0;
  if (p->params.on_start) {
    p->params.on_start(p->params.userdata, p->type);
  }

  struct json2exo_info info;
  err = json2exo(
      &(struct json2exo_params){
          .fp = p->params.fp,
          .editp = p->params.editp,
          .json_path = json_path,
          .exo_path = exo_path,
          .lua_directory = lua_directory,
          .module = module,
          .userdata = p,
          .on_progress = on_progress,
          .on_log_line = on_log_line,
      },
      &info);
  if (p->params.on_create_exo) {
    p->params.on_create_exo(p->params.userdata,
                            &(struct processor_exo_info){
                                .exo_path = exo_path,
                                .length = info.frames,
                                .layer_min = info.layer_min,
                                .layer_max = info.layer_max,
                            });
  }
cleanup:
  OV_ARRAY_DESTROY(&lua_directory);
  OV_ARRAY_DESTROY(&exo_path);
  OV_ARRAY_DESTROY(&json_path);
  bool const r = esucceeded(err);
  if (p->params.on_finish) {
    p->params.on_finish(p->params.userdata, p->type, err);
  } else {
    ereport(err);
  }
  return r;
}

static int run(void *userdata) {
  struct processor *const p = userdata;
  bool r;
  p->aborted = false;
  r = run_raw2opus(p, false);
  if (!r || p->aborted) {
    goto cleanup;
  }
  r = run_opus2json(p, false);
  if (!r || p->aborted) {
    goto cleanup;
  }
  r = run_json2exo(p, false);
  if (!r || p->aborted) {
    goto cleanup;
  }
cleanup:
  if (p->params.on_complete) {
    p->params.on_complete(p->params.userdata, r && !p->aborted);
  }
  ereport(remove_temporary_files(p->params.hinst));
  return 0;
}

NODISCARD error processor_run(struct processor *const p) {
  if (!p) {
    return errg(err_invalid_arugment);
  }
  error err = eok();
  if (thrd_create(&p->thread, run, p) != thrd_success) {
    err = emsg_i18n(err_type_generic, err_fail, gettext("Unable to create thread."));
    goto cleanup;
  }
cleanup:
  return err;
}

static int run_solo(void *userdata) {
  struct processor *const p = userdata;
  bool r = false;
  p->aborted = false;
  p->progress = 0;
  switch ((int)p->type) {
  case processor_type_raw2opus:
    r = run_raw2opus(p, true);
    break;
  case processor_type_opus2json:
    r = run_opus2json(p, true);
    break;
  case processor_type_json2exo:
    r = run_json2exo(p, true);
    break;
  }
  if (p->params.on_complete) {
    p->params.on_complete(p->params.userdata, r && !p->aborted);
  }
  return 0;
}

NODISCARD error processor_run_solo(struct processor *const p, enum processor_type const type) {
  if (!p) {
    return errg(err_invalid_arugment);
  }
  error err = eok();
  p->type = type;
  if (thrd_create(&p->thread, run_solo, p) != thrd_success) {
    err = emsg_i18n(err_type_generic, err_fail, gettext("Unable to create thread."));
    goto cleanup;
  }
cleanup:
  if (efailed(err)) {
    p->type = processor_type_invalid;
  }
  return err;
}

NODISCARD error processor_abort(struct processor *const p) {
  if (!p) {
    return errg(err_invalid_arugment);
  }
  p->aborted = true;
  return eok();
}

static NODISCARD error test_module(lua_State *const L,
                                   wchar_t const *const module,
                                   wchar_t **const name,
                                   wchar_t **const description) {
  int const top = lua_gettop(L);
  error err = lua_require(L, module);
  if (efailed(err)) {
    err = ethru(err);
    goto cleanup;
  }
  if (!lua_istable(L, -1)) {
    err = emsg_i18nf(err_type_generic, err_lua, L"%1$hs", "%1$hs", gettext("The module did not return a table."));
    goto cleanup;
  }
  lua_getfield(L, -1, "get_info");
  if (!lua_isfunction(L, -1)) {
    err = emsg_i18nf(err_type_generic, err_lua, L"%1$hs", gettext("\"%1$hs\" is not a function."), "get_info");
    goto cleanup;
  }
  if (lua_safecall(L, 0, 1)) {
    err = ethru(err);
    goto cleanup;
  }
  lua_getfield(L, -1, "name");
  if (!lua_isstring(L, -1)) {
    err = emsg_i18nf(err_type_generic, err_lua, L"%1$hs", gettext("\"%1$hs\" is not a string."), "name");
    goto cleanup;
  }
  lua_getfield(L, -2, "description");
  if (!lua_isstring(L, -1)) {
    err = emsg_i18nf(err_type_generic, err_lua, L"%1$hs", gettext("\"%1$hs\" is not a string."), "description");
    goto cleanup;
  }
  size_t len;
  char const *s = lua_tolstring(L, -2, &len);
  int wlen = MultiByteToWideChar(CP_UTF8, 0, s, (int)len, NULL, 0);
  if (wlen == 0) {
    err = errhr(HRESULT_FROM_WIN32(GetLastError()));
    goto cleanup;
  }
  err = OV_ARRAY_GROW(name, wlen + 1);
  if (efailed(err)) {
    err = ethru(err);
    goto cleanup;
  }
  if (MultiByteToWideChar(CP_UTF8, 0, s, (int)len, *name, wlen) == 0) {
    err = errhr(HRESULT_FROM_WIN32(GetLastError()));
    goto cleanup;
  }
  (*name)[wlen] = L'\0';
  s = lua_tolstring(L, -1, &len);
  wlen = MultiByteToWideChar(CP_UTF8, 0, s, (int)len, NULL, 0);
  if (wlen == 0) {
    err = errhr(HRESULT_FROM_WIN32(GetLastError()));
    goto cleanup;
  }
  err = OV_ARRAY_GROW(description, wlen + 1);
  if (efailed(err)) {
    err = ethru(err);
    goto cleanup;
  }
  if (MultiByteToWideChar(CP_UTF8, 0, s, (int)len, *description, wlen) == 0) {
    err = errhr(HRESULT_FROM_WIN32(GetLastError()));
    goto cleanup;
  }
  (*description)[wlen] = L'\0';
cleanup:
  lua_settop(L, top);
  return err;
}

static void report_error(struct processor *const p, error e) {
  wchar_t *r = e->msg.ptr, *l = r;
  while (*r) {
    if (*r != L'\r' && *r != L'\n') {
      ++r;
      continue;
    }
    wchar_t *sep = r;
    r += (r[0] == L'\r' && r[1] == L'\n') ? 2 : 1;
    *sep = L'\0';
    p->params.on_log_line(p->params.userdata, p->type, l);
    l = r;
  }
  if (l < r) {
    p->params.on_log_line(p->params.userdata, p->type, l);
  }
  efree(&e);
}

NODISCARD error processor_get_modules(struct processor *const p, struct processor_module **const pmpp) {
  if (!p || !pmpp) {
    return errg(err_invalid_arugment);
  }

  wchar_t *dir = NULL;
  struct processor_module *pm = NULL;
  wchar_t *strings = NULL;
  wchar_t *name = NULL;
  wchar_t *description = NULL;
  struct luactx *ctx = NULL;
  HANDLE h = INVALID_HANDLE_VALUE;

  error err = get_lua_directory(&dir, p->params.hinst);
  if (efailed(err)) {
    err = ethru(err);
    goto cleanup;
  }
  size_t const luadirlen = wcslen(dir);
  err = OV_ARRAY_GROW(&dir, luadirlen + MAX_PATH + 32);
  if (efailed(err)) {
    err = ethru(err);
    goto cleanup;
  }
  err = luactx_create(&ctx,
                      &(struct luactx_params){
                          .lua_directory = dir,
                          .userdata = p,
                          .on_log_line = on_log_line,
                      });
  if (efailed(err)) {
    err = ethru(err);
    goto cleanup;
  }

  lua_State *L = luactx_get(ctx);
  wcscpy(dir + luadirlen, L"\\*");
  size_t num_modules = 0;

  WIN32_FIND_DATAW find_data;
  h = FindFirstFileW(dir, &find_data);
  if (h == INVALID_HANDLE_VALUE) {
    err = errhr(HRESULT_FROM_WIN32(GetLastError()));
    goto cleanup;
  }
  do {
    if (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
      if (wcscmp(find_data.cFileName, L".") == 0 || wcscmp(find_data.cFileName, L"..") == 0) {
        continue;
      }
      wcscpy(dir + luadirlen, L"\\");
      wcscpy(dir + luadirlen + 1, find_data.cFileName);
      wcscpy(dir + luadirlen + 1 + wcslen(find_data.cFileName), L"\\init.lua");
      if (GetFileAttributesW(dir) == INVALID_FILE_ATTRIBUTES) {
        continue;
      }
    } else {
      wchar_t *dot = wcsrchr(find_data.cFileName, L'.');
      if (!dot) {
        continue;
      }
      if (wcscmp(dot, L".lua") != 0) {
        continue;
      }
      *dot = L'\0';
    }
    err = test_module(L, find_data.cFileName, &name, &description);
    if (efailed(err)) {
      if (eisg(err, err_lua)) {
        wchar_t msg[2048];
        mo_snprintf_wchar(
            msg, 2048, L"%1$ls", gettext("[WARN] Package \"%1$ls\" cannot be used as a module."), find_data.cFileName);
        p->params.on_log_line(p->params.userdata, p->type, msg);
        report_error(p, err);
        continue;
      }
      err = ethru(err);
      goto cleanup;
    }
    size_t const modulelen = wcslen(find_data.cFileName);
    size_t const namelen = wcslen(name);
    size_t const descriptionlen = wcslen(description);
    size_t n = OV_ARRAY_LENGTH(strings);
    err = OV_ARRAY_GROW(&strings, n + (modulelen + namelen + descriptionlen + 3));
    if (efailed(err)) {
      err = ethru(err);
      goto cleanup;
    }
    wcscpy(strings + n, find_data.cFileName);
    n += modulelen + 1;
    wcscpy(strings + n, name);
    n += namelen + 1;
    wcscpy(strings + n, description);
    n += descriptionlen + 1;
    OV_ARRAY_SET_LENGTH(strings, n);
    ++num_modules;
  } while (FindNextFileW(h, &find_data));

  err = OV_ARRAY_GROW(&pm, num_modules);
  if (efailed(err)) {
    err = ethru(err);
    goto cleanup;
  }
  wchar_t const *s = strings;
  for (size_t i = 0; i < num_modules; ++i) {
    pm[i].module = s;
    s += wcslen(s) + 1;
    pm[i].name = s;
    s += wcslen(s) + 1;
    pm[i].description = s;
    s += wcslen(s) + 1;
  }
  OV_ARRAY_SET_LENGTH(pm, num_modules);
  *pmpp = pm;
  pm = NULL;
  strings = NULL;
cleanup:
  if (h != INVALID_HANDLE_VALUE) {
    FindClose(h);
    h = INVALID_HANDLE_VALUE;
  }
  if (ctx) {
    luactx_destroy(&ctx);
  }
  OV_ARRAY_DESTROY(&name);
  OV_ARRAY_DESTROY(&description);
  OV_ARRAY_DESTROY(&dir);
  OV_ARRAY_DESTROY(&strings);
  return err;
}

void processor_module_destroy(struct processor_module **const pmpp) {
  if (!pmpp || !*pmpp) {
    return;
  }
  struct processor_module *pm = *pmpp;
  if (OV_ARRAY_LENGTH(pm) > 0) {
    wchar_t *s = ov_deconster_(pm[0].module);
    OV_ARRAY_DESTROY(&s);
  }
  OV_ARRAY_DESTROY(pmpp);
}
