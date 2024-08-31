#include "luactx.h"

#include <ovarray.h>
#include <ovutil/win32.h>

#include <lauxlib.h>
#include <lualib.h>

#include "aviutl.h"
#include "i18n.h"
#include "process.h"

static int g_key = 0;

struct luactx {
  lua_State *L;
  struct process_line_buffer_context line_buffer;
  struct luactx_params params;
  char *preferred_languages;
  wchar_t *buffer;
};

static void *lua_alloc(void *const userdata, void *ptr, size_t const old_size, size_t const new_size) {
  (void)userdata;
  (void)old_size;
  if (new_size) {
    if (!ereport(mem(&ptr, new_size, 1))) {
      return NULL;
    }
    return ptr;
  }
  ereport(mem_free(&ptr));
  return NULL;
}

static int lua_throw_error(lua_State *const L, error e, char const *const funcname) {
  struct wstr msg = {0};
  struct wstr errmsg = {0};
  struct str s = {0};

  luaL_where(L, 1);
  if (strncmp(funcname, "lua_", 4) == 0) {
    lua_pushstring(L, "error on Subtitler.");
    lua_pushstring(L, funcname + 4);
  } else {
    lua_pushstring(L, "error on ");
    lua_pushstring(L, funcname);
  }
  lua_pushstring(L, "():\r\n");
  error err = error_to_string(e, &errmsg);
  if (efailed(err)) {
    err = ethru(err);
    goto cleanup;
  }
  err = scat(&msg, errmsg.ptr);
  if (efailed(err)) {
    err = ethru(err);
    goto cleanup;
  }
  err = to_utf8(&errmsg, &s);
  if (efailed(err)) {
    err = ethru(err);
    goto cleanup;
  }
  lua_pushlstring(L, s.ptr, s.len);
  lua_concat(L, 5);

cleanup:
  if (efailed(err)) {
    efree(&err);
    lua_pushstring(L, gettext("Unable to build error message."));
    lua_concat(L, 5);
  }
  ereport(sfree(&msg));
  ereport(sfree(&errmsg));
  ereport(sfree(&s));
  efree(&e);
  return lua_error(L);
}
#define lua_throw(L, err) lua_throw_error((L), (err), (__func__))

NODISCARD error lua_pcall_(lua_State *const L, int const nargs, int const nresults ERR_FILEPOS_PARAMS) {
  if (lua_pcall(L, nargs, nresults, 0) == 0) {
    return eok();
  }
  error err = eok();
  struct wstr trace = {0};
  size_t trace_ansi_len;
  char const *trace_ansi = lua_tolstring(L, -1, &trace_ansi_len);
  if (!trace_ansi) {
    err = emsg_i18n(err_type_generic, err_fail, gettext("Unable to retrieve trace message."));
    goto cleanup;
  }
  err = to_wstr(trace_ansi, trace_ansi_len, &trace);
  if (efailed(err)) {
    err = ethru(err);
    goto cleanup;
  }
  err = error_add_(NULL, err_type_generic, err_lua, &trace ERR_FILEPOS_VALUES_PASSTHRU);
  trace = (struct wstr){0};
cleanup:
  ereport(sfree(&trace));
  return err;
}

NODISCARD error lua_require(lua_State *const L, wchar_t const *const module_name) {
  error err = eok();
  char *buf = NULL;
  int len = WideCharToMultiByte(CP_ACP, 0, module_name, -1, NULL, 0, NULL, NULL);
  if (len == 0) {
    err = errhr(HRESULT_FROM_WIN32(GetLastError()));
    goto cleanup;
  }
  err = OV_ARRAY_GROW(&buf, len);
  if (efailed(err)) {
    err = ethru(err);
    goto cleanup;
  }
  OV_ARRAY_SET_LENGTH(buf, (size_t)len);
  if (WideCharToMultiByte(CP_ACP, 0, module_name, -1, buf, len, NULL, NULL) == 0) {
    err = errhr(HRESULT_FROM_WIN32(GetLastError()));
    goto cleanup;
  }

  lua_getglobal(L, "require");
  lua_pushstring(L, buf);
  err = lua_safecall(L, 1, 1);
  if (efailed(err)) {
    err = ethru(err);
    goto cleanup;
  }
cleanup:
  OV_ARRAY_DESTROY(&buf);
  return err;
}

static void process_line(void *const userdata, char const *const message) {
  struct luactx *const ctx = userdata;
  if (ctx->params.on_log_line) {
    int len = MultiByteToWideChar(CP_UTF8, 0, message, -1, NULL, 0);
    if (len == 0) {
      ereport(errhr(HRESULT_FROM_WIN32(GetLastError())));
      return;
    }
    error err = OV_ARRAY_GROW(&ctx->buffer, len);
    if (efailed(err)) {
      ereport(err);
      return;
    }
    if (MultiByteToWideChar(CP_UTF8, 0, message, -1, ctx->buffer, len) == 0) {
      ereport(errhr(HRESULT_FROM_WIN32(GetLastError())));
      return;
    }
    ctx->params.on_log_line(ctx->params.userdata, ctx->buffer);
  }
}

static int lua_debug_print(lua_State *L) {
  error err = eok();
  int const nargs = lua_gettop(L);
  if (nargs != 1) {
    err = errg(err_invalid_arugment);
    goto cleanup;
  }
  size_t len;
  char const *s = lua_tolstring(L, 1, &len);
  if (!s) {
    err = errg(err_invalid_arugment);
    goto cleanup;
  }
  lua_pushlightuserdata(L, (void *)&g_key);
  lua_gettable(L, LUA_REGISTRYINDEX);
  struct luactx *ctx = lua_touserdata(L, -1);
  process_line_buffer(&ctx->line_buffer, s, len);
  if (s[len - 1] != '\n') {
    process_line_buffer(&ctx->line_buffer, "\n", 1);
  }
cleanup:
  return efailed(err) ? lua_throw(L, err) : 0;
}

static int luafn_exotext(lua_State *const L) {
  struct wstr tmp = {0};
  error err = eok();
  int const nargs = lua_gettop(L);
  if (nargs != 1) {
    err = errg(err_invalid_arugment);
    goto cleanup;
  }
  err = from_utf8(&str_unmanaged_const(lua_tostring(L, 1)), &tmp);
  if (efailed(err)) {
    err = ethru(err);
    goto cleanup;
  }
  char buf[4096];
  static char const hex[] = "0123456789abcdef";
  size_t len = tmp.len;
  for (size_t i = 0; i < len; ++i) {
    int const c = tmp.ptr[i];
    buf[i * 4 + 0] = hex[(c >> 4) & 0xf];
    buf[i * 4 + 1] = hex[(c >> 0) & 0xf];
    buf[i * 4 + 2] = hex[(c >> 12) & 0xf];
    buf[i * 4 + 3] = hex[(c >> 8) & 0xf];
  }
  memset(buf + len * 4, '0', 4096 - len * 4);
  lua_pushlstring(L, buf, 4096);
cleanup:
  ereport(sfree(&tmp));
  return efailed(err) ? lua_throw(L, err) : 1;
}

static NODISCARD error get_preferred_languages_in_utf8(char **langs) {
  error err = eok();
  struct wstr tmp = {0};
  err = mo_get_preferred_ui_languages(&tmp);
  if (efailed(err)) {
    err = ethru(err);
    goto cleanup;
  }
  int len = WideCharToMultiByte(CP_UTF8, 0, tmp.ptr, (int)tmp.len, NULL, 0, NULL, NULL);
  if (len == 0) {
    err = errhr(HRESULT_FROM_WIN32(GetLastError()));
    goto cleanup;
  }
  err = OV_ARRAY_GROW(langs, len);
  if (efailed(err)) {
    err = ethru(err);
    goto cleanup;
  }
  if (WideCharToMultiByte(CP_UTF8, 0, tmp.ptr, (int)tmp.len, *langs, len, NULL, NULL) == 0) {
    err = errhr(HRESULT_FROM_WIN32(GetLastError()));
    goto cleanup;
  }
  OV_ARRAY_SET_LENGTH(*langs, (size_t)len);
cleanup:
  ereport(sfree(&tmp));
  return err;
}

static bool find_preferred_language(lua_State *const L, int const tableidx, char const *const langs) {
  char const *lang = langs;
  while (lang) {
    lua_getfield(L, tableidx, lang);
    if (lua_isstring(L, -1)) {
      return true;
    }
    lua_pop(L, 1);
    lang += strlen(lang) + 1;
  }
  return false;
}

static int luafn_i18n(lua_State *const L) {
  error err = eok();
  int const nargs = lua_gettop(L);
  if (nargs != 1) {
    err = errg(err_invalid_arugment);
    goto cleanup;
  }
  if (!lua_istable(L, 1)) {
    err = errg(err_invalid_arugment);
    goto cleanup;
  }

  lua_pushlightuserdata(L, (void *)&g_key);
  lua_gettable(L, LUA_REGISTRYINDEX);
  struct luactx *ctx = lua_touserdata(L, -1);
  if (!ctx) {
    err = errg(err_invalid_arugment);
    goto cleanup;
  }

  if (!ctx->preferred_languages) {
    err = get_preferred_languages_in_utf8(&ctx->preferred_languages);
    if (efailed(err)) {
      err = ethru(err);
      goto cleanup;
    }
  }
  if (find_preferred_language(L, 1, ctx->preferred_languages)) {
    goto cleanup;
  }
  if (find_preferred_language(L, 1, "en_US\0ja_JP\0")) { // fallback
    goto cleanup;
  }
  err = emsg_i18n(err_type_generic, err_fail, gettext("No language resource found."));
cleanup:
  return efailed(err) ? lua_throw(L, err) : 1;
}

NODISCARD error luactx_create(struct luactx **const lcpp, struct luactx_params const *const params) {
  if (!lcpp || *lcpp) {
    return errg(err_invalid_arugment);
  }

  char *buf = NULL;
  struct luactx *lc = NULL;
  error err = mem(&lc, 1, sizeof(struct luactx));
  if (efailed(err)) {
    err = ethru(err);
    goto cleanup;
  }
  *lc = (struct luactx){
      .params = *params,
  };
  lc->line_buffer.userdata = lc;
  lc->line_buffer.on_line = process_line;

  lc->L = lua_newstate(lua_alloc, NULL);
  if (!lc->L) {
    err = errg(err_out_of_memory);
    goto cleanup;
  }

  lua_pushlightuserdata(lc->L, (void *)&g_key);
  lua_pushlightuserdata(lc->L, lc);
  lua_settable(lc->L, LUA_REGISTRYINDEX);

  luaL_openlibs(lc->L);
  lua_pushcfunction(lc->L, lua_debug_print);
  lua_setglobal(lc->L, "debug_print");
  lua_pushcfunction(lc->L, luafn_exotext);
  lua_setglobal(lc->L, "exotext");
  lua_pushcfunction(lc->L, luafn_i18n);
  lua_setglobal(lc->L, "i18n");

  int len = WideCharToMultiByte(CP_ACP, 0, params->lua_directory, -1, NULL, 0, NULL, NULL);
  if (len == 0) {
    err = errhr(HRESULT_FROM_WIN32(GetLastError()));
    goto cleanup;
  }
  err = OV_ARRAY_GROW(&buf, (size_t)len);
  if (efailed(err)) {
    err = ethru(err);
    goto cleanup;
  }
  if (WideCharToMultiByte(CP_ACP, 0, params->lua_directory, -1, buf, len, NULL, NULL) == 0) {
    err = errhr(HRESULT_FROM_WIN32(GetLastError()));
    goto cleanup;
  }

  lua_getglobal(lc->L, "package");
  lua_pushfstring(lc->L, "%s\\?.lua;%s\\?\\init.lua", buf, buf);
  lua_setfield(lc->L, -2, "path");
  lua_pushfstring(lc->L, "%s\\?.dll", buf);
  lua_setfield(lc->L, -2, "cpath");
  lua_pop(lc->L, 1);

  *lcpp = lc;
cleanup:
  if (efailed(err)) {
    luactx_destroy(&lc);
  }
  OV_ARRAY_DESTROY(&buf);
  return err;
}

void luactx_destroy(struct luactx **const lcpp) {
  if (!lcpp || !*lcpp) {
    return;
  }
  struct luactx *lc = *lcpp;
  if (lc->L) {
    lua_close(lc->L);
  }
  OV_ARRAY_DESTROY(&lc->buffer);
  OV_ARRAY_DESTROY(&lc->preferred_languages);
  ereport(mem_free(lcpp));
}

lua_State *luactx_get(struct luactx *const lc) { return lc->L; }
