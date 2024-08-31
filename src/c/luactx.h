#pragma once

#include <ovbase.h>

#include <lua.h>

enum {
  err_lua = 0x10000,
};

struct luactx;

struct luactx_params {
  wchar_t const *lua_directory;
  void *userdata;
  void (*on_log_line)(void *const userdata, wchar_t const *const message);
};

NODISCARD error luactx_create(struct luactx **const lcpp, struct luactx_params const *const params);
void luactx_destroy(struct luactx **const lcpp);
lua_State *luactx_get(struct luactx *const lc);
NODISCARD error lua_pcall_(lua_State *const L, int const nargs, int const nresults ERR_FILEPOS_PARAMS);
#define lua_safecall(L, nargs, nresults) (lua_pcall_((L), (nargs), (nresults)ERR_FILEPOS_VALUES))

NODISCARD error lua_require(lua_State *const L, wchar_t const *const module_name);
