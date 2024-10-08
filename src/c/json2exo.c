#include "json2exo.h"

#include <stdatomic.h>

#include <ovarray.h>
#include <ovnum.h>
#include <ovprintf.h>
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
    if (on_progress) {
      err = on_progress(userdata, (int)((i + 1) * 10000 / num_segments));
      if (efailed(err)) {
        err = errg(err_abort);
        goto cleanup;
      }
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
  struct json2exo_params const *params;
  struct luactx *luactx;
  int module_index;
};

static NODISCARD error on_progress(void *const userdata, int const progress) {
  struct json2exo_context *const ctx = userdata;
  if (!ctx->params->on_progress) {
    return eok();
  }
  if (!ctx->params->on_progress(ctx->userdata, progress)) {
    return errg(err_abort);
  }
  return eok();
}

static NODISCARD error on_segment(void *const userdata, struct segment const *const segment) {
  struct json2exo_context *const ctx = userdata;
  error err = eok();
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

static bool find_used_layer_range(
    char const *const exo, int *const num_objects, int *const layer_min, int *const layer_max, int *const frames) {
  if (!exo || !layer_min || !layer_max) {
    return false;
  }
  int lmin = INT_MAX;
  int lmax = INT_MIN;
  int fmax = INT_MIN;
  int64_t v64;
  int v;
  int objects = 0;
  char const *pos = exo;
  char const *line_end = find_line_break(exo);
  enum section {
    section_unknown,
    section_header,
    section_object,
  } section = section_unknown;
  while (line_end) {
    size_t const len = (size_t)(line_end - pos);
    if (len > 2 && pos[0] == '[' && pos[len - 1] == ']') {
      if (len == 8 && memcmp(pos + 1, "exedit", 6) == 0) {
        section = section_header; // [exedit]
      } else if (is_number_only(pos + 1, len - 2)) {
        section = section_object; // [%d]
        ++objects;
      } else {
        section = section_unknown; // not interested
      }
    } else {
      if (section == section_header && len > 7 && strncmp(pos, "length=", 7) == 0 &&
          is_number_only(pos + 7, len - 7)) { // length=%d
        if (ov_atoi_char(pos + 7, &v64, false)) {
          v = (int)v64;
          if (v > fmax) {
            fmax = v;
          }
        }
      } else if (section == section_object && len > 6 && strncmp(pos, "layer=", 6) == 0 &&
                 is_number_only(pos + 6, len - 6)) { // layer=%d
        if (ov_atoi_char(pos + 6, &v64, false)) {
          v = (int)v64;
          if (v < lmin) {
            lmin = v;
          }
          if (v > lmax) {
            lmax = v;
          }
        }
      } else if (section == section_object && len > 4 && strncmp(pos, "end=", 4) == 0 &&
                 is_number_only(pos + 4, len - 4)) { // end=%d
        if (ov_atoi_char(pos + 4, &v64, false)) {
          v = (int)v64;
          if (v > fmax) {
            fmax = v;
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
  *frames = fmax;
  return true;
}

static NODISCARD error find_max_time(void *const userdata, struct segment const *const seg) {
  double *const max_time = userdata;
  if (seg->end > *max_time) {
    *max_time = seg->end;
  }
  return eok();
}

NODISCARD error json2exo(struct json2exo_params const *const params, struct json2exo_info *const info) {
  if (!params || !params->json_path || !params->lua_directory || !params->module || !params->fp || !params->editp ||
      !info) {
    return errg(err_invalid_arugment);
  }
  error err = eok();
  FILE_INFO fi;
  char *buf = NULL;
  wchar_t *wbuf = NULL;
  HANDLE json = INVALID_HANDLE_VALUE;
  HANDLE exo = INVALID_HANDLE_VALUE;

  if (params->on_log_line) {
    wchar_t msg[1024];
    mo_snprintf_wchar(msg, sizeof(msg) / sizeof(msg[0]), L"%1$ls", "Source: %1$ls", params->json_path);
    params->on_log_line(params->userdata, msg);
    mo_snprintf_wchar(msg, sizeof(msg) / sizeof(msg[0]), L"%1$ls", "Destination: %1$ls", params->exo_path);
    params->on_log_line(params->userdata, msg);
  }

  struct json2exo_context ctx = {
      .userdata = params->userdata,
      .params = params,
  };

  if (!params->fp->exfunc->get_file_info(params->editp, &fi)) {
    err = emsg_i18n(err_type_generic, err_fail, gettext("Unable to retrieve file information."));
    goto cleanup;
  }
  err = luactx_create(&ctx.luactx,
                      &(struct luactx_params){
                          .lua_directory = params->lua_directory,
                          .userdata = params->userdata,
                          .on_log_line = params->on_log_line,
                      });
  if (efailed(err)) {
    err = ethru(err);
    goto cleanup;
  }

  lua_State *L = luactx_get(ctx.luactx);
  err = lua_require(L, params->module);
  if (efailed(err)) {
    err = ethru(err);
    goto cleanup;
  }
  ctx.module_index = lua_gettop(L);

  json =
      CreateFileW(params->json_path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
  if (json == INVALID_HANDLE_VALUE) {
    HRESULT hr = HRESULT_FROM_WIN32(GetLastError());
    if (hr == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND)) {
      err = emsg_i18nf(
          err_type_generic, err_not_found, L"%1$ls", gettext("The file \"%1$ls\" is not found."), params->json_path);
    } else {
      err = errhr(hr);
    }
    goto cleanup;
  }

  double max_time = 0;
  err = parse(json, find_max_time, NULL, &max_time);
  if (efailed(err)) {
    err = ethru(err);
    goto cleanup;
  }
  if (SetFilePointer(json, 0, NULL, FILE_BEGIN) == INVALID_SET_FILE_POINTER) {
    err = errhr(HRESULT_FROM_WIN32(GetLastError()));
    goto cleanup;
  }

  lua_getfield(L, ctx.module_index, "on_start");
  if (!lua_isfunction(L, -1)) {
    err = emsg_i18nf(err_type_generic, err_fail, L"%1$hs", gettext("\"%1$hs\" is not a function."), "on_start");
    goto cleanup;
  }
  lua_newtable(L);
  lua_pushinteger(L, fi.w);
  lua_setfield(L, -2, "width");
  lua_pushinteger(L, fi.h);
  lua_setfield(L, -2, "height");
  lua_pushinteger(L, fi.video_rate);
  lua_setfield(L, -2, "rate");
  lua_pushinteger(L, fi.video_scale);
  lua_setfield(L, -2, "scale");
  lua_pushinteger(L, (int)(max_time * fi.video_rate / fi.video_scale));
  lua_setfield(L, -2, "length");
  lua_pushinteger(L, fi.audio_rate);
  lua_setfield(L, -2, "audio_rate");
  lua_pushinteger(L, fi.audio_ch);
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

  err = parse(json, on_segment, on_progress, &ctx);
  if (efailed(err)) {
    err = ethru(err);
    goto cleanup;
  }

  lua_getfield(L, ctx.module_index, "on_finalize");
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

  int num_objects = 0, lmin = INT_MAX, lmax = INT_MIN, fmax = INT_MIN;
  if (!find_used_layer_range(exo_utf8, &num_objects, &lmin, &lmax, &fmax)) {
    err = emsg_i18n(err_type_generic, err_fail, gettext("Unable to parse the *.exo."));
    goto cleanup;
  }
  if (!num_objects) {
    err = emsg_i18n(err_type_generic, err_fail, gettext("No objects found in the *.exo."));
    goto cleanup;
  }

  exo = CreateFileW(params->exo_path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
  if (exo == INVALID_HANDLE_VALUE) {
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
  if (!WriteFile(exo, buf, (DWORD)sjislen, &written, NULL)) {
    err = errhr(HRESULT_FROM_WIN32(GetLastError()));
    goto cleanup;
  }
  if (written != (DWORD)sjislen) {
    err = emsg_i18n(err_type_generic, err_fail, gettext("Unable to write the entire file."));
    goto cleanup;
  }
  *info = (struct json2exo_info){
      .frames = fmax,
      .layer_min = lmin,
      .layer_max = lmax,
      .num_objects = num_objects,
  };

cleanup:
  if (exo != INVALID_HANDLE_VALUE) {
    CloseHandle(exo);
    exo = INVALID_HANDLE_VALUE;
    if (efailed(err)) {
      DeleteFileW(params->exo_path);
    }
  }
  if (wbuf) {
    OV_ARRAY_DESTROY(&wbuf);
  }
  if (buf) {
    OV_ARRAY_DESTROY(&buf);
  }
  if (json != INVALID_HANDLE_VALUE) {
    CloseHandle(json);
    json = INVALID_HANDLE_VALUE;
  }
  if (ctx.luactx) {
    luactx_destroy(&ctx.luactx);
  }
  return err;
}
