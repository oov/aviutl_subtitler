#include "aviutl.h"

#include <ovutil/str.h>
#include <ovutil/win32.h>

#include <string.h>

#include "i18n.h"

static FILTER *g_fp = NULL;
static void *g_editp = NULL;

static FILTER *g_exedit_fp = NULL;
static enum aviutl_patched g_exedit_patch = aviutl_patched_default;

NODISCARD static error verify_installation(void) {
  struct wstr path = {0};
  error err = get_module_file_name(get_hinstance(), &path);
  if (efailed(err)) {
    err = ethru(err);
    goto cleanup;
  }

  size_t fnpos = 0;
  err = extract_file_name(&path, &fnpos);
  if (efailed(err)) {
    err = ethru(err);
    goto cleanup;
  }
  path.ptr[fnpos] = L'\0';
  path.len = fnpos;

  err = scat(&path, L"exedit.auf");
  if (efailed(err)) {
    err = ethru(err);
    goto cleanup;
  }

  bool found = false;
  err = file_exists(&path, &found);
  if (efailed(err)) {
    err = ethru(err);
    goto cleanup;
  }
  if (!found) {
    err = emsg_i18nf(
        err_type_generic, err_fail, L"%1$hs", gettext("\"%1$hs\" is not found in same directory."), "exedit.auf");
    goto cleanup;
  }

cleanup:
  ereport(sfree(&path));
  return err;
}

NODISCARD static error find_exedit_filter(FILTER **const exedit_fp, enum aviutl_patched *const patched) {
  static TCHAR const exedit_name_mbcs[] = "\x8a\x67\x92\xa3\x95\xd2\x8f\x57";              // "拡張編集"
  static TCHAR const zhcn_patched_exedit_name_mbcs[] = "\xc0\xa9\xd5\xb9\xb1\xe0\xbc\xad"; // "扩展编辑"
  static TCHAR const en_patched_exedit_name_mbcs[] = "Advanced Editing";

  *exedit_fp = NULL;
  SYS_INFO si;
  if (!g_fp->exfunc->get_sys_info(g_editp, &si)) {
    return errg(err_fail);
  }
  for (int i = 0; i < si.filter_n; ++i) {
    FILTER *p = g_fp->exfunc->get_filterp(i);
    if (!p || (p->flag & FILTER_FLAG_AUDIO_FILTER) == FILTER_FLAG_AUDIO_FILTER) {
      continue;
    }
    if (strcmp(p->name, exedit_name_mbcs) == 0) {
      *exedit_fp = p;
      *patched = aviutl_patched_default;
      return eok();
    } else if (strcmp(p->name, zhcn_patched_exedit_name_mbcs) == 0) {
      *exedit_fp = p;
      *patched = aviutl_patched_zh_cn;
      return eok();
    } else if (strcmp(p->name, en_patched_exedit_name_mbcs) == 0) {
      *exedit_fp = p;
      *patched = aviutl_patched_en;
      return eok();
    }
  }
  *exedit_fp = NULL;
  *patched = aviutl_patched_default;
  return emsg_i18nf(err_type_generic, err_fail, L"%1$hs", gettext("\"%1$hs\" is not found."), "exedit.auf");
}

NODISCARD static error verify_aviutl_version(void) {
  SYS_INFO si;
  if (!g_fp->exfunc->get_sys_info(g_editp, &si)) {
    return errg(err_fail);
  }
  if (si.build < 10000) {
    return emsg_i18nf(
        err_type_generic, err_fail, L"%1$hs", gettext("This version of %1$hs is not supported."), gettext("AviUtl"));
  }
  return eok();
}

static size_t atou32(TCHAR const *s, uint32_t *const ret) {
  uint64_t r = 0;
  size_t i = 0;
  while (s[i]) {
    if (i >= 10 || '0' > s[i] || s[i] > '9') {
      break;
    }
    r = r * 10 + (uint64_t)(s[i++] - '0');
  }
  if (i == 0 || r > 0xffffffff) {
    return 0;
  }
  *ret = r & 0xffffffff;
  return i;
}

NODISCARD static error verify_exedit_version(FILTER const *const exedit_fp) {
  static TCHAR const version_token[] = " version ";
  TCHAR const *verstr = strstr(exedit_fp->information, version_token);
  if (!verstr) {
    goto failed;
  }
  verstr += strlen(version_token);
  uint32_t major = 0, minor = 0;
  size_t len = atou32(verstr, &major);
  if (!len) {
    goto failed;
  }
  verstr += len + 1; // skip dot
  len = atou32(verstr, &minor);
  if (!len) {
    goto failed;
  }
  if (major == 0 && minor < 92) {
    goto failed;
  }
  return eok();

failed:
  return emsg_i18nf(err_type_generic,
                    err_fail,
                    L"%1$hs",
                    gettext("This version of %1$hs is not supported."),
                    gettext("Advanced Editing"));
}

void aviutl_set_pointers(FILTER *fp, void *editp) {
  g_fp = fp;
  g_editp = editp;
}

void aviutl_get_pointers(FILTER **const fp, void **const editp) {
  if (fp) {
    *fp = g_fp;
  }
  if (editp) {
    *editp = g_editp;
  }
}

error aviutl_init(void) {
  FILTER *exedit_fp = NULL;
  enum aviutl_patched patched = aviutl_patched_default;
  error err = eok();

  err = verify_installation();
  if (efailed(err)) {
    err = ethru(err);
    goto cleanup;
  }
  err = verify_aviutl_version();
  if (efailed(err)) {
    err = ethru(err);
    goto cleanup;
  }
  err = find_exedit_filter(&exedit_fp, &patched);
  if (efailed(err)) {
    err = ethru(err);
    goto cleanup;
  }
  err = verify_exedit_version(exedit_fp);
  if (efailed(err)) {
    err = ethru(err);
    goto cleanup;
  }
  g_exedit_fp = exedit_fp;
  g_exedit_patch = patched;
cleanup:
  return err;
}

bool aviutl_initalized(void) { return g_exedit_fp; }

error aviutl_exit(void) { return eok(); }

NODISCARD error aviutl_get_patch(enum aviutl_patched *const patched) {
  if (!patched) {
    return errg(err_null_pointer);
  }
  if (!aviutl_initalized()) {
    return errg(err_unexpected);
  }
  *patched = g_exedit_patch;
  return eok();
}

HWND aviutl_get_my_window(void) {
  if (g_fp) {
    return g_fp->hwnd;
  }
  wchar_t buf[1024];
  mo_snprintf_wchar(buf,
                    sizeof(buf) / sizeof(wchar_t),
                    L"%1$hs",
                    gettext("Unable to obtain window handle for %1$hs."),
                    gettext("Subtitler"));
  OutputDebugStringW(buf);
  return GetDesktopWindow();
}

static void *calc_offset(void const *const addr, size_t const offset) { return (void *)((uintptr_t)addr + offset); }

NODISCARD error aviutl_drop_exo(char const *const exo_path, int frame, int layer, int frames) {
  typedef void(__cdecl * set_end_frame_fn)(FILTER * fp, void *editp, int n);
  typedef BOOL(__cdecl * load_from_exo_fn)(char const *path, int frame, int layer, void *editp, FILTER *fp);
  int *const end_frame = (int *)(calc_offset(g_exedit_fp->dll_hinst, 0x1a5318));
  set_end_frame_fn const set_end_frame = (set_end_frame_fn)(calc_offset(g_exedit_fp->dll_hinst, 0x3b1d0));
  load_from_exo_fn const load_from_exo = (load_from_exo_fn)(calc_offset(g_exedit_fp->dll_hinst, 0x4dca0));
  if (*end_frame < frames + 1) {
    set_end_frame(g_exedit_fp, g_editp, frames + 1);
  }
  if (!load_from_exo(exo_path, frame, layer, g_editp, g_exedit_fp)) {
    return errg(err_fail);
  }
  return eok();
}

#define DEBUG_OUTPUT 0

enum {
  layer_length = 100,
  display_name_length = 64,
  filter_length = 12,
  track_length = 64,
  check_length = 48,
};

struct exedit_filter {
  uint32_t flag;
  int32_t x; // 0x10000=出力フィルタなら変更時にカメラ制御対象フラグがon(グループ制御にもついている)
  int32_t y; // デフォルト長さ(正ならフレーム数、負ならミリ秒、ゼロならタイムライン拡大率に応じた長さ)
  char const *name;
  int32_t track_num; // 0x10
  char const **track_name;
  int32_t *track_def;
  int32_t *track_min;
  int32_t *track_max; // 0x20
  int32_t check_num;
  char const **check_name;
  int32_t *check_def; // -1:ボタン化 -2:ドロップダウンリスト化
  void *func_proc;    // 0x30
  void *func_init;
  void *func_exit;
  void *func_update;
  void *func_WndProc; // 0x40
  void *track_value;
  int32_t *check_value;
  void *exdata_ptr;
  uint32_t exdata_size; // 0x50
  void *information;    // 常に NULL
  void *func_save_start;
  void *func_save_end;
  void *aviutl_exfunc; // 0x60
  void *exedit_exfunc;
  void *dll_inst;   // 常にNULL
  void *exdata_def; // 0x6C : 拡張データの初期値
  struct exdata_use {
    enum {
      exdata_use_type_padding = 0, // name は NULL
      exdata_use_type_number = 1,
      exdata_use_type_string = 2,
      exdata_use_type_bytearray = 3,
    };
    uint16_t type;
    uint16_t size;
    char const *name; // 項目名
  } *exdata_use;      // 拡張データ項目の名前に関するデータ
  struct track_extra {
    int *track_scale;
    int *track_link;
    int *track_drag_min;
    int *track_drag_max;
  } *track_extra;
  struct TRACK_GUI {
    enum {
      track_gui_invalid = -1,
    };
    int bx, by, bz; // 基準座標
    int rx, ry, rz; // 回転
    int cx, cy, cz; // 中心座標
    int zoom, aspect, alpha;
  } *track_gui;
  int32_t unknown[20];
  int32_t *track_scale; // 0xCC : 小数点第1位までなら10、第2位までなら100、小数点無しなら0
  void *track_link;
  int32_t *track_drag_min;
  int32_t *track_drag_max;
  FILTER *exedit_filter;             // 拡張編集のフィルタ構造体
  struct exedit_object *object_data; // 0xE0
  short object_index_processing;     // 処理中オブジェクトのインデックス
  short filter_pos_processing;       // 処理中オブジェクトのフィルタ位置
  short object_index_objdlg;         // 上と同じ？
  short filter_pos_objdlg;
  int32_t frame_start;       // オブジェクトの開始フレーム
  int32_t frame_end;         // 0xF0 : オブジェクトの終了フレーム
  int32_t *track_value_left; //
  int32_t *track_value_right;
  int32_t *track_mode;
  int32_t *check_value_; // 0x100
  void *exdata_;
  int32_t *track_param;
  void *offset_10C;
  void *offset_110;
  void *offset_114;
  int32_t frame_start_chain; // 0x118 : 中間点オブジェクト全体の開始フレーム
  int32_t frame_end_chain;   // 0x11C 同終了フレーム
  int32_t layer_set;         // 配置レイヤー
  int32_t scene_set;         // 配置シーン
};

struct exedit_object {
  uint32_t flag;
  int32_t layer_disp; // 表示レイヤー、別シーン表示中は-1
  int32_t frame_begin;
  int32_t frame_end;
  char display_name[display_name_length]; // タイムライン上の表示名(最初のバイトがヌル文字の場合はデフォルト名が表示)
  int32_t
      index_midpt_leader; // 中間点を持つオブジェクトの構成要素の場合、先頭オブジェクトのインデックス、中間点を持たないなら-1
  struct filter_param {
    int32_t id;
    int16_t track_begin; // このフィルタの先頭のトラックバー番号
    int16_t check_begin;
    uint32_t exdata_offset;
  } filter_param[filter_length];
  uint8_t filter_status[filter_length];
  int16_t track_sum;
  int16_t check_sum;
  uint32_t exdata_sum;
  int32_t track_value_left[track_length];
  int32_t track_value_right[track_length];
  struct track_mode {
    int16_t num;
    int16_t script_num;
  } track_mode[track_length];
  int32_t check_value[check_length];
  uint32_t exdata_offset;
  int32_t group_belong;
  int32_t track_param[track_length];
  int32_t layer_set;
  int32_t scene_set;
};

struct exedit {
  HMODULE module;

  // 全てのオブジェクトの配列の先頭へのポインター、順不同
  struct exedit_object const *const *object;
  // 総オブジェクト数
  uint32_t const *object_count;

  // 各レイヤーに配置されているオブジェクトの開始インデックスが格納されている int32_t[100] へのポインター
  int32_t const *active_scene_sorted_object_layer_begin_index;
  int32_t const *active_scene_sorted_object_layer_end_index;
  // オブジェクトの配置レイヤー番号の小さい順、開始フレーム位置が小さい順でソートされている
  struct exedit_object const *const *active_scene_sorted_object;

  // 内部フィルターとプラグインからロードしたフィルターが一緒くたになった配列
  struct exedit_filter const *const *filter_table;
  // 現在表示中のシーン番号
  int32_t const *active_scene;
  // object が持つ exdata の基底アドレス
  // [uint32_t sz][uint8_t data[sz]] の繰り返し
  void const *const *exdata_base_ptr;
};

#if DEBUG_OUTPUT
static void
get_display_name(wchar_t *const buf, size_t const buflen, struct exedit *const e, struct exedit_object const *const o) {
  char const *const display_name =
      o->display_name[0] == '\0' ? e->filter_table[o->filter_param[0].id]->name : o->display_name;
  MultiByteToWideChar(CP_ACP, 0, display_name, -1, buf, (int)buflen);
}
#endif

static bool get_exedit(struct exedit *const e) {
  if (!g_exedit_fp) {
    return false;
  }
  HMODULE h = g_exedit_fp->dll_hinst;
  *e = (struct exedit){
      .module = h,
      .object = calc_offset(h, 0x1e0fa4),
      .object_count = calc_offset(h, 0x146250),
      .active_scene_sorted_object_layer_begin_index = calc_offset(h, 0x149670),
      .active_scene_sorted_object_layer_end_index = calc_offset(h, 0x135ac8),
      .active_scene_sorted_object = calc_offset(h, 0x168fa8),
      .filter_table = calc_offset(h, 0x187c98),
      .active_scene = calc_offset(h, 0x1a5310),
      .exdata_base_ptr = calc_offset(h, 0x1e0fa8),
  };
  return true;
}

NODISCARD error aviutl_find_space(int start_frame,
                                  int end_frame,
                                  int const required_spaces,
                                  int const search_offset,
                                  bool const last,
                                  int *const found_target) {
#if DEBUG_OUTPUT
  wchar_t buf[1024];
  wchar_t namebuf[256];
#endif
  error err = eok();
  struct exedit ex;
  if (!get_exedit(&ex)) {
    err = errg(err_fail);
    goto cleanup;
  }

#if DEBUG_OUTPUT
  mo_snprintf_wchar(buf, sizeof(buf) / sizeof(wchar_t), NULL, "start_frame: %d end_frame: %d", start_frame, end_frame);
  OutputDebugStringW(buf);
#endif

  int target_layer = -1;
  int found_spaces = 0;
  for (int layer = search_offset; layer < 100; ++layer) {
    int32_t const sidx = ex.active_scene_sorted_object_layer_begin_index[layer];
    int32_t const eidx = ex.active_scene_sorted_object_layer_end_index[layer];
    bool blocked = false;
    for (int32_t oidx = sidx; oidx <= eidx; ++oidx) {
      struct exedit_object const *o = ex.active_scene_sorted_object[oidx];
#if DEBUG_OUTPUT
      get_display_name(namebuf, sizeof(namebuf) / sizeof(wchar_t), &ex, o);
      mo_snprintf_wchar(buf,
                        sizeof(buf) / sizeof(wchar_t),
                        NULL,
                        "layer: %d oidx: %d frame_begin: %d frame_end: %d name: %ls",
                        layer + 1,
                        oidx,
                        o->frame_begin,
                        o->frame_end,
                        namebuf);
      OutputDebugStringW(buf);
#endif
      int const obegin = o->frame_begin;
      int const oend = o->frame_end;
      if ((start_frame <= obegin && obegin <= end_frame) || (start_frame <= oend && oend <= end_frame) ||
          (obegin <= start_frame && end_frame <= oend)) {
#if DEBUG_OUTPUT
        mo_snprintf_wchar(buf, sizeof(buf) / sizeof(wchar_t), NULL, "blocked: layer %d", layer + 1);
        OutputDebugStringW(buf);
#endif
        blocked = true;
        found_spaces = 0;
        break;
      }
    }
    if (!blocked) {
      if (++found_spaces == required_spaces) {
        target_layer = layer - required_spaces;
        if (!last) {
          break;
        }
      }
    }
  }
#if DEBUG_OUTPUT
  mo_snprintf_wchar(buf, sizeof(buf) / sizeof(wchar_t), L"%1$d", "target_layer: %d", target_layer + 1 + 1);
  OutputDebugStringW(buf);
#endif
  if (found_target) {
    *found_target = target_layer + 1;
  }
cleanup:
  return err;
}
