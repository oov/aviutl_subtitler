#include "json2exo.h"

#include <stdatomic.h>

#include <ovarray.h>
#include <ovnum.h>
#include <ovprintf.h>
#include <ovthreads.h>
#include <ovutil/win32.h>

#include "i18n.h"
#include "jsoncommon.h"
#include "luactx.h"

struct word {
  double start;
  double end;
  char const *word;
};

struct segment {
  double start;
  double end;
  char const *text;

  struct word *words;
  size_t num_words;
};

#define VERIFY_AND_GET(out_var, obj, key, context, yytype, type)                                                       \
  do {                                                                                                                 \
    struct yyjson_val *val = yyjson_obj_get(obj, key);                                                                 \
    if (!(val) || !(yyjson_is_##yytype(val))) {                                                                        \
      return emsg_i18nf(err_type_generic,                                                                              \
                        err_fail,                                                                                      \
                        L"%1$hs%2$hs%3$hs",                                                                            \
                        gettext("%1$hs must contain a \"%2$hs\" (type: %3$hs)."),                                      \
                        context,                                                                                       \
                        key,                                                                                           \
                        type);                                                                                         \
    }                                                                                                                  \
    out_var = yyjson_get_##yytype(val);                                                                                \
  } while (0)
#define VERIFY_AND_GET_NUMBER(out_var, obj, key, context) VERIFY_AND_GET(out_var, obj, key, context, num, "number")
#define VERIFY_AND_GET_STRING(out_var, obj, key, context) VERIFY_AND_GET(out_var, obj, key, context, str, "string")

static NODISCARD error parse(HANDLE src,
                             NODISCARD error (*on_segment)(void *const userdata, struct segment const *const),
                             NODISCARD error (*on_progress)(void *const userdata, int const progress),
                             void *const userdata) {
  if (!src || src == INVALID_HANDLE_VALUE || !on_segment) {
    return errg(err_invalid_arugment);
  }

  error err = eok();
  char *json = NULL;
  struct segment segment = {0};
  struct yyjson_doc *doc = NULL;

  DWORD const size = GetFileSize(src, NULL);
  if (size == INVALID_FILE_SIZE) {
    err = errhr(HRESULT_FROM_WIN32(GetLastError()));
    goto cleanup;
  }

  err = mem(&json, size + YYJSON_PADDING_SIZE, sizeof(char));
  if (efailed(err)) {
    err = ethru(err);
    goto cleanup;
  }

  DWORD read = 0;
  if (!ReadFile(src, json, size, &read, NULL)) {
    err = errhr(HRESULT_FROM_WIN32(GetLastError()));
    goto cleanup;
  }
  if (read != size) {
    err = emsg_i18n(err_type_generic, err_fail, gettext("Unable to read the entire file."));
    goto cleanup;
  }

  struct yyjson_read_err read_err;
  doc = yyjson_read_opts(json, read, YYJSON_READ_INSITU, jsoncommon_get_json_alc(), &read_err);
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

  struct yyjson_val *segments = yyjson_obj_get(root, "segments");
  if (!segments || !yyjson_is_arr(segments)) {
    err = emsg_i18nf(err_type_generic,
                     err_fail,
                     L"%1$hs",
                     gettext("The root of the JSON must contain a \"%1$hs\" array."),
                     "segments");
    goto cleanup;
  }

  size_t i, num_segments;
  struct yyjson_val *elem;
  yyjson_arr_foreach(segments, i, num_segments, elem) {
    if (!yyjson_is_obj(elem)) {
      err = emsg_i18nf(
          err_type_generic, err_fail, L"%1$hs", gettext("\"%1$hs\" array must contain objects."), "segments");
      goto cleanup;
    }

    {
      char const *const context = gettext("\"segment\" object");
      VERIFY_AND_GET_NUMBER(segment.start, elem, "start", context);
      VERIFY_AND_GET_NUMBER(segment.end, elem, "end", context);
      VERIFY_AND_GET_STRING(segment.text, elem, "text", context);
    }

    struct yyjson_val *words = yyjson_obj_get(elem, "words");
    if (!words || !yyjson_is_arr(words)) {
      err = emsg_i18nf(err_type_generic,
                       err_fail,
                       L"%1$hs%2$hs%3$hs",
                       gettext("%1$hs must contain a \"%2$hs\" (type: %3$hs)."),
                       gettext("\"segment\" object"),
                       "words",
                       "array");
      goto cleanup;
    }

    segment.num_words = yyjson_arr_size(words);
    err = OV_ARRAY_GROW(&segment.words, segment.num_words);
    if (efailed(err)) {
      err = ethru(err);
      goto cleanup;
    }
    OV_ARRAY_SET_LENGTH(segment.words, segment.num_words);

    size_t j, num_words;
    struct yyjson_val *elem2;
    yyjson_arr_foreach(words, j, num_words, elem2) {
      if (!yyjson_is_obj(elem2)) {
        err =
            emsg_i18nf(err_type_generic, err_fail, L"%1$hs", gettext("\"%1$hs\" array must contain objects."), "words");
        goto cleanup;
      }
      struct word *const word = &segment.words[j];
      char const *const context = gettext("\"word\" object");
      VERIFY_AND_GET_NUMBER(word->start, elem2, "start", context);
      VERIFY_AND_GET_NUMBER(word->end, elem2, "end", context);
      VERIFY_AND_GET_STRING(word->word, elem2, "word", context);
    }

    err = on_segment(userdata, &segment);
    if (efailed(err)) {
      err = ethru(err);
      goto cleanup;
    }
    err = on_progress(userdata, (int)((i + 1) * 10000 / num_segments));
    if (efailed(err)) {
      err = errg(err_abort);
      goto cleanup;
    }
  }
cleanup:
  if (segment.words) {
    OV_ARRAY_DESTROY(&segment.words);
  }
  if (doc) {
    yyjson_doc_free(doc);
    doc = NULL;
  }
  if (json) {
    ereport(mem_free(&json));
  }
  return err;
}

struct json2exo_context {
  void *userdata;
  void (*on_progress)(void *const userdata, int const progress);
  void (*on_log_line)(void *const userdata, wchar_t const *const message);
  void (*on_finish)(void *const userdata, struct json2exo_info const *const info, error err);
  thrd_t thread;
  FILE_INFO fi;
  atomic_bool abort_requested;
  struct luactx *luactx;
  int module_index;
  HANDLE json;
  wchar_t *exo_path;
  int start_frame;
  int end_frame;
};

static NODISCARD error on_progress(void *const userdata, int const progress) {
  struct json2exo_context *const ctx = userdata;
  error err = eok();
  if (atomic_load(&ctx->abort_requested)) {
    err = errg(err_abort);
    goto cleanup;
  }
  if (ctx->on_progress) {
    ctx->on_progress(ctx->userdata, progress);
  }
cleanup:
  return err;
}

static NODISCARD error on_segment(void *const userdata, struct segment const *const segment) {
  struct json2exo_context *const ctx = userdata;
  error err = eok();
  if (atomic_load(&ctx->abort_requested)) {
    err = errg(err_abort);
    goto cleanup;
  }
  lua_State *L = luactx_get(ctx->luactx);
  lua_getfield(L, ctx->module_index, "on_segment");
  if (!lua_isfunction(L, -1)) {
    err = emsg_i18nf(err_type_generic, err_fail, L"%1$hs", gettext("\"%1$hs\" is not a function."), "on_segment");
    goto cleanup;
  }
  lua_newtable(L);
  lua_pushnumber(L, segment->start);
  lua_setfield(L, -2, "start");
  lua_pushnumber(L, segment->end);
  lua_setfield(L, -2, "end");
  lua_pushstring(L, segment->text);
  lua_setfield(L, -2, "text");
  lua_newtable(L);
  for (size_t i = 0; i < segment->num_words; ++i) {
    struct word const *const word = &segment->words[i];
    lua_newtable(L);
    lua_pushnumber(L, word->start);
    lua_setfield(L, -2, "start");
    lua_pushnumber(L, word->end);
    lua_setfield(L, -2, "end");
    lua_pushstring(L, word->word);
    lua_setfield(L, -2, "word");
    lua_rawseti(L, -2, (int)i + 1);
  }
  lua_setfield(L, -2, "words");
  err = lua_safecall(L, 1, 1);
  if (efailed(err)) {
    err = ethru(err);
    goto cleanup;
  }
  if (!lua_toboolean(L, -1)) {
    err =
        emsg_i18nf(err_type_generic, err_abort, L"%1$hs", gettext("\"%1$hs\" function returned false."), "on_segment");
    goto cleanup;
  }
  lua_pop(L, 1);
cleanup:
  return err;
}

static bool is_line_break(int const ch) { return ch == '\r' || ch == '\n'; }

static char const *find_line_break(char const *s) {
  if (!s) {
    return NULL;
  }
  while (*s) {
    if (is_line_break(*s)) {
      return s;
    }
    ++s;
  }
  return NULL;
}

static char const *skip_line_break(char const *s) {
  if (!s) {
    return NULL;
  }
  while (*s && is_line_break(*s)) {
    ++s;
  }
  return s;
}

static bool is_number(int const ch) { return ch >= '0' && ch <= '9'; }

static bool is_number_only(char const *s, size_t const len) {
  if (!s) {
    return false;
  }
  for (size_t i = 0; i < len; ++i) {
    if (!is_number(s[i])) {
      return false;
    }
  }
  return true;
}

static bool
find_used_layer_range(char const *const exo, int *const num_objects, int *const layer_min, int *const layer_max) {
  if (!exo || !layer_min || !layer_max) {
    return false;
  }
  int lmin = INT_MAX;
  int lmax = INT_MIN;
  int layer;
  int objects = 0;
  char const *pos = exo;
  char const *line_end = find_line_break(exo);
  bool object_section = false;
  while (line_end) {
    size_t const len = (size_t)(line_end - pos);
    if (len > 2 && pos[0] == '[' && pos[len - 1] == ']') {
      // find [%d]
      object_section = is_number_only(pos + 1, len - 2);
      if (object_section) {
        ++objects;
      }
    } else if (object_section && len > 6 && strncmp(pos, "layer=", 6) == 0) {
      // find layer=%d
      if (is_number_only(pos + 6, len - 6)) {
        int64_t v;
        if (ov_atoi_char(pos + 6, &v, false)) {
          layer = (int)v;
          if (layer < lmin) {
            lmin = layer;
          }
          if (layer > lmax) {
            lmax = layer;
          }
        }
      }
    }
    pos = skip_line_break(line_end);
    line_end = find_line_break(pos);
  }
  *num_objects = objects;
  *layer_min = lmin;
  *layer_max = lmax;
  return true;
}

static int worker(void *const userdata) {
  struct json2exo_context *ctx = userdata;

  error err = eok();
  char *buf = NULL;
  wchar_t *wbuf = NULL;
  HANDLE h = INVALID_HANDLE_VALUE;
  int num_objects = 0, lmin = INT_MAX, lmax = INT_MIN;

  lua_State *L = luactx_get(ctx->luactx);
  lua_getfield(L, ctx->module_index, "on_start");
  if (!lua_isfunction(L, -1)) {
    err = emsg_i18nf(err_type_generic, err_fail, L"%1$hs", gettext("\"%1$hs\" is not a function."), "on_start");
    goto cleanup;
  }
  lua_newtable(L);
  lua_pushinteger(L, ctx->fi.w);
  lua_setfield(L, -2, "width");
  lua_pushinteger(L, ctx->fi.h);
  lua_setfield(L, -2, "height");
  lua_pushinteger(L, ctx->fi.video_rate);
  lua_setfield(L, -2, "rate");
  lua_pushinteger(L, ctx->fi.video_scale);
  lua_setfield(L, -2, "scale");
  lua_pushinteger(L, ctx->fi.frame_n);
  lua_setfield(L, -2, "length");
  lua_pushinteger(L, ctx->fi.audio_rate);
  lua_setfield(L, -2, "audio_rate");
  lua_pushinteger(L, ctx->fi.audio_ch);
  lua_setfield(L, -2, "audio_ch");
  err = lua_safecall(L, 1, 1);
  if (efailed(err)) {
    err = ethru(err);
    goto cleanup;
  }
  if (!lua_toboolean(L, -1)) {
    err = emsg_i18nf(err_type_generic, err_abort, L"%1$hs", gettext("\"%1$hs\" function returned false."), "on_start");
    goto cleanup;
  }
  lua_pop(L, 1);

  err = parse(ctx->json, on_segment, on_progress, ctx);
  if (efailed(err)) {
    err = ethru(err);
    goto cleanup;
  }

  lua_getfield(L, ctx->module_index, "on_finalize");
  if (!lua_isfunction(L, -1)) {
    err = emsg_i18nf(err_type_generic, err_fail, L"%1$hs", gettext("\"%1$hs\" is not a function."), "on_finalize");
    goto cleanup;
  }
  err = lua_safecall(L, 0, 1);
  if (efailed(err)) {
    err = ethru(err);
    goto cleanup;
  }
  size_t exo_utf8_len;
  char const *exo_utf8 = lua_tolstring(L, -1, &exo_utf8_len);
  if (!exo_utf8) {
    err = emsg_i18nf(err_type_generic, err_fail, L"%1$hs", gettext("\"%1$hs\" must return a string."), "on_finalize");
    goto cleanup;
  }

  if (!find_used_layer_range(exo_utf8, &num_objects, &lmin, &lmax)) {
    err = emsg_i18n(err_type_generic, err_fail, gettext("Unable to parse the *.exo."));
    goto cleanup;
  }
  if (!num_objects) {
    err = emsg_i18n(err_type_generic, err_fail, gettext("No objects found in the *.exo."));
    goto cleanup;
  }

  h = CreateFileW(ctx->exo_path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
  if (h == INVALID_HANDLE_VALUE) {
    err = errhr(HRESULT_FROM_WIN32(GetLastError()));
    goto cleanup;
  }
  int wlen = MultiByteToWideChar(CP_UTF8, 0, exo_utf8, (int)exo_utf8_len, NULL, 0);
  if (wlen == 0) {
    err = errhr(HRESULT_FROM_WIN32(GetLastError()));
    goto cleanup;
  }
  err = OV_ARRAY_GROW(&wbuf, wlen);
  if (efailed(err)) {
    err = ethru(err);
    goto cleanup;
  }
  if (MultiByteToWideChar(CP_UTF8, 0, exo_utf8, (int)exo_utf8_len, wbuf, wlen) == 0) {
    err = errhr(HRESULT_FROM_WIN32(GetLastError()));
    goto cleanup;
  }
  // The contents of *.exo should be Shift_JIS.
  // When loading in an environment with a different code page,
  // it is expected to be translated by the GCMZDrops conversion function.
  enum {
    CP_SHIFT_JIS = 932,
  };
  int sjislen = WideCharToMultiByte(CP_SHIFT_JIS, 0, wbuf, wlen, NULL, 0, NULL, NULL);
  if (sjislen == 0) {
    err = errhr(HRESULT_FROM_WIN32(GetLastError()));
    goto cleanup;
  }
  err = OV_ARRAY_GROW(&buf, sjislen);
  if (efailed(err)) {
    err = ethru(err);
    goto cleanup;
  }
  if (WideCharToMultiByte(CP_SHIFT_JIS, 0, wbuf, wlen, buf, sjislen, NULL, NULL) == 0) {
    err = errhr(HRESULT_FROM_WIN32(GetLastError()));
    goto cleanup;
  }

  DWORD written = 0;
  if (!WriteFile(h, buf, (DWORD)sjislen, &written, NULL)) {
    err = errhr(HRESULT_FROM_WIN32(GetLastError()));
    goto cleanup;
  }
  if (written != (DWORD)sjislen) {
    err = emsg_i18n(err_type_generic, err_fail, gettext("Unable to write the entire file."));
    goto cleanup;
  }
cleanup:
  if (h != INVALID_HANDLE_VALUE) {
    CloseHandle(h);
    h = INVALID_HANDLE_VALUE;
    if (efailed(err)) {
      DeleteFileW(ctx->exo_path);
    }
  }
  if (wbuf) {
    OV_ARRAY_DESTROY(&wbuf);
  }
  if (buf) {
    OV_ARRAY_DESTROY(&buf);
  }
  if (ctx->json) {
    CloseHandle(ctx->json);
    ctx->json = INVALID_HANDLE_VALUE;
  }
  if (ctx->on_finish) {
    ctx->on_finish(ctx->userdata, efailed(err) ? NULL : &(struct json2exo_info){
      .exo_path = ctx->exo_path,
      .start_frame = ctx->start_frame,
      .end_frame = ctx->end_frame,
                         .layer_min = lmin,
                         .layer_max = lmax,
                         .num_objects = num_objects,
                     }, err);
  }
  return 0;
}

NODISCARD error json2exo_create(struct json2exo_context **const ctxpp, struct json2exo_params *params) {
  if (!ctxpp || *ctxpp || !params || !params->json_path || !params->lua_directory || !params->module || !params->fp ||
      !params->editp) {
    return errg(err_invalid_arugment);
  }

  struct json2exo_context *ctx = NULL;
  error err = mem(&ctx, 1, sizeof(struct json2exo_context));
  if (efailed(err)) {
    err = ethru(err);
    goto cleanup;
  }
  *ctx = (struct json2exo_context){
      .userdata = params->userdata,
      .on_progress = params->on_progress,
      .on_log_line = params->on_log_line,
      .on_finish = params->on_finish,
  };
  atomic_init(&ctx->abort_requested, false);

  {
    FILTER *const fp = params->fp;
    void *const editp = params->editp;
    if (!fp->exfunc->get_file_info(editp, &ctx->fi)) {
      err = emsg_i18n(err_type_generic, err_fail, gettext("Unable to retrieve file information."));
      goto cleanup;
    }
    int s, e;
    if (!fp->exfunc->get_select_frame(editp, &s, &e)) {
      e = fp->exfunc->get_frame_n(editp);
      s = 0;
    }
    ctx->start_frame = s;
    ctx->end_frame = e;
    ctx->fi.frame_n = e - s + 1;
  }

  err = luactx_create(&ctx->luactx,
                      &(struct luactx_params){
                          .lua_directory = params->lua_directory,
                          .userdata = params->userdata,
                          .on_log_line = params->on_log_line,
                      });
  if (efailed(err)) {
    err = ethru(err);
    goto cleanup;
  }

  lua_State *L = luactx_get(ctx->luactx);
  err = lua_require(L, params->module);
  if (efailed(err)) {
    err = ethru(err);
    goto cleanup;
  }
  ctx->module_index = lua_gettop(L);

  ctx->json =
      CreateFileW(params->json_path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
  if (ctx->json == INVALID_HANDLE_VALUE) {
    HRESULT hr = HRESULT_FROM_WIN32(GetLastError());
    if (hr == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND)) {
      err = emsg_i18nf(
          err_type_generic, err_not_found, L"%1$ls", gettext("The file \"%1$ls\" is not found."), params->json_path);
    } else {
      err = errhr(hr);
    }
    goto cleanup;
  }

  size_t len = wcslen(params->exo_path);
  err = OV_ARRAY_GROW(&ctx->exo_path, len + 1);
  if (efailed(err)) {
    err = ethru(err);
    goto cleanup;
  }
  OV_ARRAY_SET_LENGTH(ctx->exo_path, len);
  wcscpy(ctx->exo_path, params->exo_path);

  if (ctx->on_log_line) {
    wchar_t buf[1024];
    mo_snprintf_wchar(buf,
                      sizeof(buf) / sizeof(buf[0]),
                      L"%1$ls",
                      gettext("Generating *.exo file with module \"%1$ls\"..."),
                      params->module);
    ctx->on_log_line(ctx->userdata, buf);
    ctx->on_log_line(ctx->userdata, ctx->exo_path);
  }

  if (thrd_create(&ctx->thread, worker, ctx) != thrd_success) {
    return errhr(HRESULT_FROM_WIN32(GetLastError()));
  }
  *ctxpp = ctx;
  ctx = NULL;
cleanup:
  if (ctx) {
    if (ctx->exo_path) {
      OV_ARRAY_DESTROY(&ctx->exo_path);
    }
    if (ctx->json != INVALID_HANDLE_VALUE) {
      CloseHandle(ctx->json);
      ctx->json = INVALID_HANDLE_VALUE;
    }
    if (ctx->luactx) {
      luactx_destroy(&ctx->luactx);
    }
    ereport(mem_free(&ctx));
  }
  return err;
}

void json2exo_destroy(struct json2exo_context **const ctxpp) {
  if (!ctxpp || !*ctxpp) {
    return;
  }
  struct json2exo_context *const ctx = *ctxpp;
  thrd_join(ctx->thread, NULL);
  if (ctx->exo_path) {
    OV_ARRAY_DESTROY(&ctx->exo_path);
  }
  if (ctx->luactx) {
    luactx_destroy(&ctx->luactx);
  }
  ereport(mem_free(ctxpp));
}

NODISCARD error json2exo_abort(struct json2exo_context *const ctx) {
  if (!ctx) {
    return errg(err_invalid_arugment);
  }
  atomic_store(&ctx->abort_requested, true);
  return eok();
}
