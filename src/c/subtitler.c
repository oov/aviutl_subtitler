#include <ovbase.h>

#include <ovarray.h>
#include <ovprintf.h>
#include <ovthreads.h>
#include <ovutf.h>
#include <ovutil/win32.h>

#include "aviutl.h"
#include "i18n.h"
#include "json2exo.h"
#include "luactx.h"
#include "opus2json.h"
#include "path.h"
#include "processor.h"
#include "version.h"

#include <commctrl.h>

enum {
  id_btn_start = 1,
  id_btn_raw2opus = 2,
  id_btn_opus2json = 3,
  id_btn_json2exo = 4,
  id_btn_abort = 5,
  id_edt_exe_path = 6,
  id_btn_exe_path = 7,
  id_edt_model_dir = 8,
  id_btn_model_dir = 9,
  id_tab = 100,
  id_tmr_progress = 101,

  WM_PROCESS_LOG_LINE = WM_USER + 0x1000,
  WM_PROCESS_FINISHED = WM_USER + 0x1001,
  WM_PROCESS_NEXT_TASK = WM_USER + 0x1002,
  WM_PROCESS_PROGRESS = WM_USER + 0x1003,
  WM_PROCESS_CREATE_EXO = WM_USER + 0x1004,
  WM_PROCESS_UPDATED = WM_USER + 0x1005,
};

enum gui_state {
  gui_state_invalid,
  gui_state_no_project,
  gui_state_ready,
  gui_state_invalid_exe_path,
  gui_state_running,
};

static wchar_t const models[] = L"tiny,tiny.en,base,base.en,small,small.en,medium,medium.en,large-v1,large-v2,large-v3,"
                                L"large,distil-large-v2,distil-medium.en,distil-small.en,distil-large-v3";
static wchar_t const languages[] =
    L"Afrikaans,Albanian,Amharic,Arabic,Armenian,Assamese,Azerbaijani,Bashkir,Basque,Belarusian,Bengali,Bosnian,Breton,"
    L"Bulgarian,Burmese,Cantonese,Castilian,Catalan,Chinese,Croatian,Czech,Danish,Dutch,English,Estonian,Faroese,"
    L"Finnish,Flemish,French,Galician,Georgian,German,Greek,Gujarati,Haitian,Haitian "
    L"Creole,Hausa,Hawaiian,Hebrew,Hindi,Hungarian,Icelandic,Indonesian,Italian,Japanese,Javanese,Kannada,Kazakh,Khmer,"
    L"Korean,Lao,Latin,Latvian,Letzeburgesch,Lingala,Lithuanian,Luxembourgish,Macedonian,Malagasy,Malay,Malayalam,"
    L"Maltese,Mandarin,Maori,Marathi,Moldavian,Moldovan,Mongolian,Myanmar,Nepali,Norwegian,Nynorsk,Occitan,Panjabi,"
    L"Pashto,Persian,Polish,Portuguese,Punjabi,Pushto,Romanian,Russian,Sanskrit,Serbian,Shona,Sindhi,Sinhala,Sinhalese,"
    L"Slovak,Slovenian,Somali,Spanish,Sundanese,Swahili,Swedish,Tagalog,Tajik,Tamil,Tatar,Telugu,Thai,Tibetan,Turkish,"
    L"Turkmen,Ukrainian,Urdu,Uzbek,Valencian,Vietnamese,Welsh,Yiddish,Yoruba";

static HWND g_tab = NULL;
static HWND g_pane_main = NULL;
static HWND g_pane_settings = NULL;
static HWND g_pane_advanced = NULL;

static HWND g_lbl_model = NULL;
static HWND g_cmb_model = NULL;
static HWND g_lbl_language = NULL;
static HWND g_cmb_language = NULL;
static HWND g_lbl_initial_prompt = NULL;
static HWND g_edt_initial_prompt = NULL;
static HWND g_lbl_insert_position = NULL;
static HWND g_cmb_insert_position = NULL;
static HWND g_lbl_insert_mode = NULL;
static HWND g_cmb_insert_mode = NULL;
static HWND g_lbl_module = NULL;
static HWND g_cmb_module = NULL;
static HWND g_btn_start = NULL;

static HWND g_lbl_exe_path = NULL;
static HWND g_edt_exe_path = NULL;
static HWND g_btn_exe_path = NULL;
static HWND g_lbl_exe_path_error = NULL;
static HWND g_lbl_model_dir = NULL;
static HWND g_edt_model_dir = NULL;
static HWND g_btn_model_dir = NULL;
static HWND g_lbl_additional_args = NULL;
static HWND g_edt_additional_args = NULL;

static HWND g_lbl_description = NULL;
static HWND g_btn_raw2opus = NULL;
static HWND g_btn_opus2json = NULL;
static HWND g_btn_json2exo = NULL;
static HWND g_btn_abort = NULL;

static HWND g_progress = NULL;
static HWND g_logview = NULL;

static mtx_t g_mtx;
static cnd_t g_cnd;
static struct mo *g_mp = NULL;
static bool g_log_processed = false;
static bool g_exo_processed = false;
static DWORD g_gui_thread_id = 0;
static wchar_t *g_log_buffer = NULL;

static struct progress {
  ULONGLONG started_at;
  ULONGLONG prev_updated_at;
  ULONGLONG last_updated_at;
  enum processor_type type;
  int prev_progress;
  int last_progress;
  bool solo;
} g_progress_info = {0};

static struct processor_context *g_processor = NULL;
static struct processor_module *g_modules = NULL;
static HWND *g_disabled_windows = NULL;

static void update_state(enum gui_state const s);

static NODISCARD error config_to_gui(struct config const *const cfg, bool const apply_to_main_cfg) {
  error err = eok();
  SetWindowTextW(g_cmb_model, config_get_model(cfg));
  SetWindowTextW(g_cmb_language, config_get_language(cfg));
  SetWindowTextW(g_edt_initial_prompt, config_get_initial_prompt(cfg));

  for (size_t i = 0, n = OV_ARRAY_LENGTH(g_modules); i < n; ++i) {
    if (wcscmp(g_modules[i].name, config_get_module(cfg)) == 0) {
      SendMessageW(g_cmb_module, CB_SETCURSEL, i, 0);
      break;
    }
  }

  int v;
  v = config_get_insert_position(cfg);
  if (v < 1 || v > 100) {
    v = 1;
  }
  SendMessageW(g_cmb_insert_position, CB_SETCURSEL, (WPARAM)(v - 1), 0);
  v = config_get_insert_mode(cfg);
  if (v < 1 || v > 2) {
    v = 1;
  }
  SendMessageW(g_cmb_insert_mode, CB_SETCURSEL, (WPARAM)(v - 1), 0);

  if (!apply_to_main_cfg) {
    goto cleanup;
  }
  struct config *const g_main_cfg = processor_get_config(g_processor);
#define SET_STRING_ITEM(PROP)                                                                                          \
  do {                                                                                                                 \
    err = config_set_##PROP(g_main_cfg, config_get_##PROP(cfg));                                                       \
    if (efailed(err)) {                                                                                                \
      err = ethru(err);                                                                                                \
      goto cleanup;                                                                                                    \
    }                                                                                                                  \
  } while (0)
#define SET_INT_ITEM(PROP)                                                                                             \
  do {                                                                                                                 \
    err = config_set_##PROP(g_main_cfg, config_get_##PROP(cfg));                                                       \
    if (efailed(err)) {                                                                                                \
      err = ethru(err);                                                                                                \
      goto cleanup;                                                                                                    \
    }                                                                                                                  \
  } while (0)
  SET_STRING_ITEM(model);
  SET_STRING_ITEM(language);
  SET_STRING_ITEM(module);
  SET_STRING_ITEM(initial_prompt);
  SET_INT_ITEM(insert_position);
  SET_INT_ITEM(insert_mode);
#undef SET_STRING_ITEM
#undef SET_INT_ITEM
cleanup:
  return err;
}

static NODISCARD error gui_to_config(struct config *const cfg) {
  wchar_t *buf = NULL;
  error err = eok();
#define SET_STRING_ITEM(CONTROL, SETFUNC)                                                                              \
  do {                                                                                                                 \
    int len = GetWindowTextLengthW(CONTROL);                                                                           \
    err = OV_ARRAY_GROW(&buf, len + 1);                                                                                \
    if (efailed(err)) {                                                                                                \
      err = ethru(err);                                                                                                \
      goto cleanup;                                                                                                    \
    }                                                                                                                  \
    GetWindowTextW(CONTROL, buf, len + 1);                                                                             \
    err = SETFUNC(cfg, buf);                                                                                           \
    if (efailed(err)) {                                                                                                \
      err = ethru(err);                                                                                                \
      goto cleanup;                                                                                                    \
    }                                                                                                                  \
  } while (0)
#define SET_INT_ITEM(CONTROL, SETFUNC)                                                                                 \
  do {                                                                                                                 \
    int const index = SendMessageW(CONTROL, CB_GETCURSEL, 0, 0);                                                       \
    if (index == CB_ERR) {                                                                                             \
      err = errhr(HRESULT_FROM_WIN32(GetLastError()));                                                                 \
      goto cleanup;                                                                                                    \
    }                                                                                                                  \
    err = SETFUNC(cfg, index + 1);                                                                                     \
    if (efailed(err)) {                                                                                                \
      err = ethru(err);                                                                                                \
      goto cleanup;                                                                                                    \
    }                                                                                                                  \
  } while (0)
  SET_STRING_ITEM(g_cmb_model, config_set_model);
  SET_STRING_ITEM(g_cmb_language, config_set_language);
  SET_STRING_ITEM(g_edt_initial_prompt, config_set_initial_prompt);
  SET_STRING_ITEM(g_edt_model_dir, config_set_model_dir);
  SET_STRING_ITEM(g_edt_additional_args, config_set_additional_args);
  SET_INT_ITEM(g_cmb_insert_position, config_set_insert_position);
  SET_INT_ITEM(g_cmb_insert_mode, config_set_insert_mode);
#undef SET_STRING_ITEM
#undef SET_INT_ITEM
  int const index = SendMessageW(g_cmb_module, CB_GETCURSEL, 0, 0);
  if (index != CB_ERR) {
    err = config_set_module(cfg, g_modules[index].module);
    if (efailed(err)) {
      err = ethru(err);
      goto cleanup;
    }
  }
cleanup:
  OV_ARRAY_DESTROY(&buf);
  return err;
}

static void reset_gui(void) {
  struct config *cfg = NULL;
  error err = config_create(&cfg);
  if (efailed(err)) {
    err = ethru(err);
    goto cleanup;
  }
  err = config_to_gui(cfg, true);
  if (efailed(err)) {
    err = ethru(err);
    goto cleanup;
  }
cleanup:
  if (cfg) {
    config_destroy(&cfg);
  }
  ereport(err);
}

static void update_title(void) {
  mtx_lock(&g_mtx);
  struct progress pi = g_progress_info;
  mtx_unlock(&g_mtx);
  ULONGLONG now = GetTickCount64();

  char const *title = gettext("Subtitler");

  char const *msg = NULL;
  switch (pi.type) {
  case processor_type_invalid:
    msg = "";
    break;
  case processor_type_raw2opus:
    msg = gettext("Encoding audio to Opus...");
    break;
  case processor_type_opus2json:
    msg = gettext("Generating JSON from Opus...");
    break;
  case processor_type_json2exo:
    msg = gettext("Converting JSON to EXO...");
    break;
  }

  wchar_t buf[1024];
  if (pi.type == processor_type_invalid) {
    mo_snprintf_wchar(buf, sizeof(buf) / sizeof(wchar_t), L"%1$hs", "%1$hs", title);
    SendMessageW(g_progress, PBM_SETPOS, 0, 0);
  } else {
    int cur_phase = 0, total_phase = 0;
    if (pi.solo) {
      cur_phase = 1;
      total_phase = 1;
    } else {
      cur_phase = (int)pi.type;
      total_phase = 3;
    }

    char remain[512];
    if (pi.last_progress > 0) {
      int const elapsed = (int)(now - pi.last_updated_at);
      int const total = (int)(pi.last_updated_at - pi.started_at);
      int const remain_progress = 10000 - pi.last_progress;
      int const remain_secs = (total * remain_progress / pi.last_progress - elapsed) / 1000;
      int const predicted_progress = pi.last_progress + (total ? (elapsed * pi.last_progress) / total : 0);
      if (remain_secs <= 0 || pi.last_progress >= 10000) {
        mo_snprintf_char(remain, sizeof(remain) / sizeof(char), "", gettext("Remaining: Almost done"));
      } else if (remain_secs < 60) {
        mo_snprintf_char(remain, sizeof(remain) / sizeof(char), "%1$d", gettext("Remaining: %1$ds"), remain_secs);
      } else if (remain_secs < 60 * 60) {
        int const m = remain_secs / 60;
        int const s = remain_secs % 60;
        mo_snprintf_char(remain, sizeof(remain) / sizeof(char), "%1$d%2$02d", gettext("Remaining: %1$dm%2$02ds"), m, s);
      } else {
        int const h = remain_secs / (60 * 60);
        int const m = (remain_secs % (60 * 60)) / 60;
        int const s = remain_secs % 60;
        mo_snprintf_char(remain,
                         sizeof(remain) / sizeof(char),
                         "%1$d%2$02d%3$02d",
                         gettext("Remaining: %1$dh%2$02dm%3$02ds"),
                         h,
                         m,
                         s);
      }
      SendMessageW(g_progress, PBM_SETPOS, (WPARAM)(predicted_progress), 0);
    } else {
      char dots[4];
      int const num_dots = (int)((now / 1000) % 4);
      for (int i = 0; i < num_dots; ++i) {
        dots[i] = '.';
      }
      dots[num_dots] = '\0';
      mo_snprintf_char(remain, sizeof(remain) / sizeof(char), "%1$hs", gettext("Remaining: Calculating%1$hs"), dots);
      SendMessageW(g_progress, PBM_SETPOS, 0, 0);
    }

    mo_snprintf_wchar(buf,
                      sizeof(buf) / sizeof(wchar_t),
                      L"%1$hs%2$d%3$d%4$hs%5$hs%6$hs",
                      "%1$hs - (%2$d/%3$d) %4$hs [%5$hs]",
                      title,
                      cur_phase,
                      total_phase,
                      msg,
                      remain);
  }
  SetWindowTextW(aviutl_get_my_window(), buf);
}

static void add_log(wchar_t const *const message) {
  size_t len = wcslen(message);
  error err = OV_ARRAY_GROW(&g_log_buffer, len + 32);
  if (efailed(err)) {
    ereport(err);
    return;
  }
  SYSTEMTIME st;
  GetLocalTime(&st);
  int n = wsprintfW(g_log_buffer,
                    L"%04d-%02d-%02d %02d:%02d:%02d.%03d  ",
                    st.wYear,
                    st.wMonth,
                    st.wDay,
                    st.wHour,
                    st.wMinute,
                    st.wSecond,
                    st.wMilliseconds);
  wcscpy(g_log_buffer + n, message);
  SendMessageW(g_logview, LB_ADDSTRING, 0, (LPARAM)g_log_buffer);
  int count = SendMessageW(g_logview, LB_GETCOUNT, 0, 0);
  if (count > 1024) {
    SendMessageW(g_logview, LB_DELETESTRING, 0, 0);
    --count;
  }
  SendMessageW(g_logview, LB_SETCURSEL, (WPARAM)(count - 1), 0);
}

static void on_next_task(void *const userdata, enum processor_type const type) {
  (void)userdata;
  PostMessageW(aviutl_get_my_window(), WM_PROCESS_NEXT_TASK, 0, (LPARAM)type);
}

static void on_progress(void *const userdata, int const progress) {
  (void)userdata;
  ULONGLONG const now = GetTickCount64();
  mtx_lock(&g_mtx);
  int const diff = (int)(now - g_progress_info.last_updated_at);
  // If the frequency is too high, it will affect the processing speed.
  // So it will be thinned out appropriately.
  if (diff > 200) {
    g_progress_info.prev_updated_at = g_progress_info.last_updated_at;
    g_progress_info.prev_progress = g_progress_info.last_progress;
    g_progress_info.last_updated_at = now;
    g_progress_info.last_progress = progress;
    PostMessageW(aviutl_get_my_window(), WM_PROCESS_PROGRESS, 0, 0);
  }
  mtx_unlock(&g_mtx);
}

static void on_create_exo(void *const userdata, struct processor_exo_info const *const info) {
  mtx_lock(&g_mtx);
  g_exo_processed = false;
  PostMessageW(aviutl_get_my_window(), WM_PROCESS_CREATE_EXO, (WPARAM)userdata, (LPARAM)info);
  while (!g_exo_processed) {
    cnd_wait(&g_cnd, &g_mtx);
  }
  mtx_unlock(&g_mtx);
}

static void on_finish(void *const userdata, error e) {
  PostMessageW(aviutl_get_my_window(), WM_PROCESS_FINISHED, (WPARAM)userdata, (LPARAM)e);
}

static void on_log_line(void *const userdata, wchar_t const *const message) {
  if (g_gui_thread_id == GetCurrentThreadId()) {
    SendMessageW(aviutl_get_my_window(), WM_PROCESS_LOG_LINE, (WPARAM)userdata, (LPARAM)message);
    return;
  }
  mtx_lock(&g_mtx);
  g_log_processed = false;
  PostMessageW(aviutl_get_my_window(), WM_PROCESS_LOG_LINE, (WPARAM)userdata, (LPARAM)message);
  while (!g_log_processed) {
    cnd_wait(&g_cnd, &g_mtx);
  }
  mtx_unlock(&g_mtx);
}

static void create_exo(void *const userdata, struct processor_exo_info const *const info) {
  (void)userdata;
  struct config const *const cfg = processor_get_config(g_processor);
  char *s = NULL;
  int layer;
  error err = aviutl_find_space(info->start_frame,
                                info->end_frame,
                                info->layer_max,
                                config_get_insert_position(cfg) - 1,
                                config_get_insert_mode(cfg) == 2,
                                &layer);
  if (efailed(err)) {
    ereport(err);
    return;
  }
  int len = WideCharToMultiByte(CP_ACP, 0, info->exo_path, -1, NULL, 0, NULL, NULL);
  if (len == 0) {
    err = errhr(HRESULT_FROM_WIN32(GetLastError()));
    goto cleanup;
  }
  err = OV_ARRAY_GROW(&s, len);
  if (efailed(err)) {
    err = ethru(err);
    goto cleanup;
  }
  if (WideCharToMultiByte(CP_ACP, 0, info->exo_path, -1, s, len, NULL, NULL) == 0) {
    err = errhr(HRESULT_FROM_WIN32(GetLastError()));
    goto cleanup;
  }
  err = aviutl_drop_exo(s, info->start_frame, layer, info->end_frame);
  if (efailed(err)) {
    err = ethru(err);
    goto cleanup;
  }
  PostMessageW(aviutl_get_my_window(), WM_PROCESS_UPDATED, 0, 0);
cleanup:
  ereport(err);
  OV_ARRAY_DESTROY(&s);
}

static void error_to_log(error e) {
  struct NATIVE_STR msg = {0};
  error err = eisg(e, err_lua) ? error_to_string_short(e, &msg) : error_to_string(e, &msg);
  if (efailed(err)) {
    err = ethru(err);
    goto cleanup;
  }
  wchar_t *r = msg.ptr, *l = r;
  while (*r) {
    if (*r != L'\r' && *r != L'\n') {
      ++r;
      continue;
    }
    wchar_t *sep = r;
    r += (r[0] == L'\r' && r[1] == L'\n') ? 2 : 1;
    *sep = L'\0';
    add_log(l);
    l = r;
  }
  if (l != r) {
    add_log(l);
  }
  ereport(e);

cleanup:
  if (efailed(err)) {
    ereport(err);
  }
  ereport(sfree(&msg));
}

static void finish(void *const userdata, error e) {
  (void)userdata;
  wchar_t buf[1024];
  error err = eok();
  processor_clean(g_processor);
  update_state(gui_state_ready);
  if (g_disabled_windows) {
    restore_disabled_family_windows(g_disabled_windows);
    g_disabled_windows = NULL;
  }
  if (esucceeded(e)) {
    mo_snprintf_wchar(buf, sizeof(buf) / sizeof(wchar_t), L"%1$ls", gettext("Operation completed successfully."));
    add_log(buf);
    goto cleanup;
  }
  if (eisg(e, err_abort)) {
    efree(&e);
    mo_snprintf_wchar(buf, sizeof(buf) / sizeof(wchar_t), L"%1$ls", gettext("Aborted."));
    add_log(buf);
    goto cleanup;
  }
  mo_snprintf_wchar(buf, sizeof(buf) / sizeof(wchar_t), L"%1$ls", gettext("Operation failed."));
  add_log(buf);
  error_to_log(e);
cleanup:
  if (efailed(err)) {
    ereport(err);
  }
  mtx_lock(&g_mtx);
  g_progress_info = (struct progress){
      .type = processor_type_invalid,
  };
  mtx_unlock(&g_mtx);
  update_title();
  KillTimer(aviutl_get_my_window(), id_tmr_progress);
}

static void run(enum processor_type const type) {
  error err = gui_to_config(processor_get_config(g_processor));
  if (efailed(err)) {
    err = ethru(err);
    goto cleanup;
  }
  err = disable_family_windows(aviutl_get_my_window(), &g_disabled_windows);
  if (efailed(err)) {
    err = ethru(err);
    goto cleanup;
  }
  NODISCARD error (*fn)(struct processor_context *const) = NULL;
  switch (type) {
  case processor_type_invalid:
    fn = processor_run;
    break;
  case processor_type_raw2opus:
    fn = processor_run_raw2opus;
    break;
  case processor_type_opus2json:
    fn = processor_run_opus2json;
    break;
  case processor_type_json2exo:
    fn = processor_run_json2exo;
    break;
  }
  err = fn(g_processor);
  if (efailed(err)) {
    err = ethru(err);
    goto cleanup;
  }
  update_state(gui_state_running);
  mtx_lock(&g_mtx);
  ULONGLONG const now = GetTickCount64();
  g_progress_info = (struct progress){
      .started_at = now,
      .last_updated_at = now,
      .prev_updated_at = now,
      .type = type == processor_type_invalid ? processor_type_raw2opus : type,
      .solo = type != processor_type_invalid,
  };
  mtx_unlock(&g_mtx);
  update_title();
  SetTimer(aviutl_get_my_window(), id_tmr_progress, 1000, NULL);
cleanup:
  if (efailed(err)) {
    if (g_disabled_windows) {
      restore_disabled_family_windows(g_disabled_windows);
      g_disabled_windows = NULL;
    }
    finish(NULL, err);
  }
}

static void push_start_or_abort(void) {
  mtx_lock(&g_mtx);
  enum processor_type const type = g_progress_info.type;
  mtx_unlock(&g_mtx);
  error err = eok();
  if (type == processor_type_invalid) {
    run(processor_type_invalid);
  } else {
    err = processor_abort(g_processor);
    if (efailed(err)) {
      err = ethru(err);
      goto cleanup;
    }
  }
cleanup:
  if (efailed(err)) {
    ereport(err);
  }
}

static void update_exe_path(void) {
  wchar_t *buf = NULL;
  error err = eok();
  size_t const len = (size_t)(GetWindowTextLengthW(g_edt_exe_path));
  if (!len) {
    err = emsg_i18nf(err_type_generic,
                     err_fail,
                     L"%1$hs",
                     gettext("Please enter the path to %1$hs executable."),
                     gettext("Whisper"));
    goto cleanup;
  }
  err = OV_ARRAY_GROW(&buf, len + 1);
  if (efailed(err)) {
    err = ethru(err);
    goto cleanup;
  }
  GetWindowTextW(g_edt_exe_path, buf, (int)(len + 1));

  struct config *cfg = processor_get_config(g_processor);
  err = config_set_whisper_path(cfg, buf);
  if (efailed(err)) {
    err = ethru(err);
    goto cleanup;
  }
  err = config_verify_whisper_path(cfg);
  if (efailed(err)) {
    err = ethru(err);
    goto cleanup;
  }
  SetWindowTextW(g_lbl_exe_path_error, L"");
cleanup:
  if (efailed(err)) {
    SetWindowTextW(g_lbl_exe_path_error, err->msg.ptr);
    efree(&err);
  }
  OV_ARRAY_DESTROY(&buf);
  update_state(gui_state_ready);
}

static void browse_exe_path(void) {
  static GUID const whisper_path_tag = {0x7f1441cf, 0xd02b, 0x4dde, {0x91, 0xc3, 0x0f, 0x2c, 0x2d, 0x93, 0x02, 0x34}};
  wchar_t title[512];
  wchar_t *path = NULL;
  HWND *windows = NULL;
  error err = disable_family_windows(aviutl_get_my_window(), &windows);
  if (efailed(err)) {
    err = ethru(err);
    goto cleanup;
  }
  mo_snprintf_wchar(
      title, sizeof(title) / sizeof(wchar_t), L"%1$hs", gettext("Select %1$hs executable"), gettext("Whisper"));
  err = path_select_file(aviutl_get_my_window(), title, L"Executable file (*.exe)\0*.exe\0", &whisper_path_tag, &path);
  if (efailed(err)) {
    err = ethru(err);
    goto cleanup;
  }
  SetWindowTextW(g_edt_exe_path, path);
  update_exe_path();
cleanup:
  restore_disabled_family_windows(windows);
  if (path) {
    OV_ARRAY_DESTROY(&path);
  }
  ereport(err);
}

static void browser_model_dir(void) {
  static GUID const model_dir_tag = {0x272816e3, 0xcbd1, 0x4dcd, {0x8b, 0xd9, 0x27, 0xd5, 0xe6, 0xe4, 0xfb, 0x0f}};
  wchar_t title[512];
  wchar_t *path = NULL;
  HWND *windows = NULL;
  error err = disable_family_windows(aviutl_get_my_window(), &windows);
  if (efailed(err)) {
    err = ethru(err);
    goto cleanup;
  }
  mo_snprintf_wchar(title, sizeof(title) / sizeof(wchar_t), L"%1$hs", gettext("Select model directory"));
  err = path_select_folder(aviutl_get_my_window(), title, &model_dir_tag, &path);
  if (efailed(err)) {
    err = ethru(err);
    goto cleanup;
  }
  SetWindowTextW(g_edt_model_dir, path);
  err = gui_to_config(processor_get_config(g_processor));
  if (efailed(err)) {
    err = ethru(err);
    goto cleanup;
  }
cleanup:
  restore_disabled_family_windows(windows);
  if (path) {
    OV_ARRAY_DESTROY(&path);
  }
  ereport(err);
}

static LRESULT CALLBACK
subclass_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam, UINT_PTR subclass_id, DWORD_PTR ref_data) {
  (void)subclass_id;
  (void)ref_data;
  switch (message) {
  case WM_COMMAND:
    switch (LOWORD(wparam)) {
    case id_btn_start:
      push_start_or_abort();
      break;
    case id_btn_raw2opus:
      run(processor_type_raw2opus);
      break;
    case id_btn_opus2json:
      run(processor_type_opus2json);
      break;
    case id_btn_json2exo:
      run(processor_type_json2exo);
      break;
    case id_edt_exe_path:
      if (HIWORD(wparam) == EN_CHANGE) {
        update_exe_path();
      }
      break;
    case id_btn_exe_path:
      browse_exe_path();
      break;
    case id_btn_model_dir:
      browser_model_dir();
      break;
    }
    break;
  }
  return DefSubclassProc(window, message, wparam, lparam);
}

static inline HWND create_window(DWORD dwExStyle,
                                 LPCWSTR lpClassName,
                                 LPCWSTR lpWindowName,
                                 DWORD dwStyle,
                                 HWND hWndParent,
                                 HMENU hMenu,
                                 HINSTANCE hInstance) {
  return CreateWindowExW(dwExStyle, lpClassName, lpWindowName, dwStyle, 0, 0, 0, 0, hWndParent, hMenu, hInstance, NULL);
}

static void add_items_to_combobox(HWND const combobox, wchar_t const *const items) {
  wchar_t buf[256];
  wchar_t const *left = items;
  while (*left) {
    wchar_t const *right = left;
    while (*right && *right != L',') {
      ++right;
    }
    size_t len = (size_t)(right - left);
    memcpy(buf, left, len * sizeof(wchar_t));
    buf[len] = L'\0';
    SendMessageW(combobox, CB_ADDSTRING, 0, (LPARAM)buf);
    left = *right ? right + 1 : right;
  }
}

static void update_state(enum gui_state s) {
  if (s == gui_state_ready && GetWindowTextLengthW(g_lbl_exe_path_error) > 0) {
    s = gui_state_invalid_exe_path;
  }
  EnableWindow(g_tab, TRUE);
  EnableWindow(g_pane_main, s == gui_state_ready || s == gui_state_running);
  EnableWindow(g_pane_settings,
               s == gui_state_ready || s == gui_state_no_project || s == gui_state_invalid_exe_path ||
                   s == gui_state_running);
  EnableWindow(g_pane_advanced, s == gui_state_ready || s == gui_state_running);

  EnableWindow(g_lbl_model, s == gui_state_ready);
  EnableWindow(g_cmb_model, s == gui_state_ready);
  EnableWindow(g_lbl_language, s == gui_state_ready);
  EnableWindow(g_cmb_language, s == gui_state_ready);
  EnableWindow(g_lbl_initial_prompt, s == gui_state_ready);
  EnableWindow(g_edt_initial_prompt, s == gui_state_ready);
  EnableWindow(g_lbl_insert_position, s == gui_state_ready);
  EnableWindow(g_cmb_insert_position, s == gui_state_ready);
  EnableWindow(g_lbl_insert_mode, s == gui_state_ready);
  EnableWindow(g_cmb_insert_mode, s == gui_state_ready);
  EnableWindow(g_lbl_module, s == gui_state_ready);
  EnableWindow(g_cmb_module, s == gui_state_ready);

  EnableWindow(g_btn_start, s == gui_state_ready || s == gui_state_running);

  EnableWindow(g_lbl_exe_path, s == gui_state_ready || s == gui_state_no_project || s == gui_state_invalid_exe_path);
  EnableWindow(g_edt_exe_path, s == gui_state_ready || s == gui_state_no_project || s == gui_state_invalid_exe_path);
  EnableWindow(g_btn_exe_path, s == gui_state_ready || s == gui_state_no_project || s == gui_state_invalid_exe_path);
  EnableWindow(g_lbl_exe_path_error,
               s == gui_state_ready || s == gui_state_no_project || s == gui_state_invalid_exe_path);
  EnableWindow(g_lbl_model_dir, s == gui_state_ready);
  EnableWindow(g_edt_model_dir, s == gui_state_ready);
  EnableWindow(g_btn_model_dir, s == gui_state_ready);
  EnableWindow(g_lbl_additional_args, s == gui_state_ready);
  EnableWindow(g_edt_additional_args, s == gui_state_ready);

  EnableWindow(g_lbl_description, s == gui_state_ready);
  EnableWindow(g_btn_raw2opus, s == gui_state_ready);
  EnableWindow(g_btn_opus2json, s == gui_state_ready);
  EnableWindow(g_btn_json2exo, s == gui_state_ready);

  EnableWindow(g_progress, s == gui_state_ready || s == gui_state_running);
  EnableWindow(g_logview, TRUE);

  wchar_t buf[1024];
  mo_snprintf_wchar(
      buf, sizeof(buf) / sizeof(wchar_t), L"%1$ls", s == gui_state_running ? gettext("Abort") : gettext("Start"));
  SetWindowTextW(g_btn_start, buf);
}

static void filter_gui_init(HWND const window, void *const editp, FILTER *const fp) {
  (void)editp;
  wchar_t buf[1024];
  HINSTANCE hInstance = get_hinstance();

  g_tab = create_window(
      0, WC_TABCONTROLW, NULL, WS_CHILD | WS_VISIBLE | TCS_SINGLELINE | TCS_BUTTONS, window, (HMENU)id_tab, hInstance);
  TCITEMW item = {
      .mask = TCIF_TEXT,
      .pszText = buf,
  };
  mo_snprintf_wchar(buf, sizeof(buf) / sizeof(wchar_t), L"%1$hs", "%1$hs", gettext("Generate"));
  SendMessageW(g_tab, TCM_INSERTITEMW, 0, (LPARAM)&item);
  mo_snprintf_wchar(buf, sizeof(buf) / sizeof(wchar_t), L"%1$hs", "%1$hs", gettext("Global Settings"));
  SendMessageW(g_tab, TCM_INSERTITEMW, 1, (LPARAM)&item);
  mo_snprintf_wchar(buf, sizeof(buf) / sizeof(wchar_t), L"%1$hs", "%1$hs", gettext("Advanced"));
  SendMessageW(g_tab, TCM_INSERTITEMW, 2, (LPARAM)&item);

  g_pane_main = create_window(0, WC_STATICW, NULL, WS_CHILD | WS_VISIBLE, g_tab, NULL, hInstance);
  SetWindowSubclass(g_pane_main, subclass_proc, (UINT_PTR)subclass_proc, 0);
  g_pane_settings = create_window(0, WC_STATICW, NULL, WS_CHILD, g_tab, NULL, hInstance);
  SetWindowSubclass(g_pane_settings, subclass_proc, (UINT_PTR)subclass_proc, 0);
  g_pane_advanced = create_window(0, WC_STATICW, NULL, WS_CHILD, g_tab, NULL, hInstance);
  SetWindowSubclass(g_pane_advanced, subclass_proc, (UINT_PTR)subclass_proc, 0);

  mo_snprintf_wchar(buf, sizeof(buf) / sizeof(wchar_t), L"%1$hs", "%1$hs", gettext("Model:"));
  g_lbl_model = create_window(0, WC_STATICW, buf, WS_CHILD | WS_VISIBLE, g_pane_main, NULL, hInstance);
  g_cmb_model = create_window(0,
                              WC_COMBOBOXW,
                              NULL,
                              WS_CHILD | WS_VISIBLE | WS_VSCROLL | CBS_DROPDOWN | CBS_AUTOHSCROLL | CBS_DISABLENOSCROLL,
                              g_pane_main,
                              NULL,
                              hInstance);
  add_items_to_combobox(g_cmb_model, models);

  mo_snprintf_wchar(buf, sizeof(buf) / sizeof(wchar_t), L"%1$hs", "%1$hs", gettext("Language:"));
  g_lbl_language = create_window(0, WC_STATICW, buf, WS_CHILD | WS_VISIBLE, g_pane_main, NULL, hInstance);
  g_cmb_language =
      create_window(0,
                    WC_COMBOBOXW,
                    NULL,
                    WS_CHILD | WS_VISIBLE | WS_VSCROLL | CBS_DROPDOWN | CBS_AUTOHSCROLL | CBS_DISABLENOSCROLL,
                    g_pane_main,
                    NULL,
                    hInstance);
  add_items_to_combobox(g_cmb_language, languages);
  mo_snprintf_wchar(buf, sizeof(buf) / sizeof(wchar_t), L"%1$hs", "%1$hs", gettext("Initial Prompt:"));
  g_lbl_initial_prompt = create_window(0, WC_STATICW, buf, WS_CHILD | WS_VISIBLE, g_pane_main, NULL, hInstance);
  g_edt_initial_prompt = create_window(
      WS_EX_CLIENTEDGE, WC_EDITW, NULL, WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, g_pane_main, NULL, hInstance);

  mo_snprintf_wchar(buf, sizeof(buf) / sizeof(wchar_t), L"%1$hs", "%1$hs", gettext("Insert Position:"));
  g_lbl_insert_position = create_window(0, WC_STATICW, buf, WS_CHILD | WS_VISIBLE, g_pane_main, NULL, hInstance);
  g_cmb_insert_position =
      create_window(0,
                    WC_COMBOBOXW,
                    NULL,
                    WS_CHILD | WS_VISIBLE | WS_VSCROLL | CBS_DROPDOWNLIST | CBS_AUTOHSCROLL | CBS_DISABLENOSCROLL,
                    g_pane_main,
                    NULL,
                    hInstance);
  for (int i = 1; i <= 100; ++i) {
    mo_snprintf_wchar(buf, sizeof(buf) / sizeof(wchar_t), L"%1$d", gettext("Layer %1$d and below"), i);
    SendMessageW(g_cmb_insert_position, CB_ADDSTRING, 0, (LPARAM)buf);
  }
  mo_snprintf_wchar(buf, sizeof(buf) / sizeof(wchar_t), L"%1$hs", "%1$hs", gettext("Insert Mode:"));
  g_lbl_insert_mode = create_window(0, WC_STATICW, buf, WS_CHILD | WS_VISIBLE, g_pane_main, NULL, hInstance);
  g_cmb_insert_mode =
      create_window(0,
                    WC_COMBOBOXW,
                    NULL,
                    WS_CHILD | WS_VISIBLE | WS_VSCROLL | CBS_DROPDOWNLIST | CBS_AUTOHSCROLL | CBS_DISABLENOSCROLL,
                    g_pane_main,
                    NULL,
                    hInstance);
  mo_snprintf_wchar(buf, sizeof(buf) / sizeof(wchar_t), L"%1$hs", "%1$hs", gettext("First available space"));
  SendMessageW(g_cmb_insert_mode, CB_ADDSTRING, 0, (LPARAM)buf);
  mo_snprintf_wchar(buf, sizeof(buf) / sizeof(wchar_t), L"%1$hs", "%1$hs", gettext("Last available space"));
  SendMessageW(g_cmb_insert_mode, CB_ADDSTRING, 0, (LPARAM)buf);
  mo_snprintf_wchar(buf, sizeof(buf) / sizeof(wchar_t), L"%1$hs", "%1$hs", gettext("Module:"));
  g_lbl_module = create_window(0, WC_STATICW, buf, WS_CHILD | WS_VISIBLE, g_pane_main, NULL, hInstance);
  g_cmb_module =
      create_window(0,
                    WC_COMBOBOXW,
                    NULL,
                    WS_CHILD | WS_VISIBLE | WS_VSCROLL | CBS_DROPDOWNLIST | CBS_AUTOHSCROLL | CBS_DISABLENOSCROLL,
                    g_pane_main,
                    NULL,
                    hInstance);

  mo_snprintf_wchar(buf, sizeof(buf) / sizeof(wchar_t), L"%1$hs", "%1$hs", gettext("Start"));
  g_btn_start = create_window(
      0, WC_BUTTONW, buf, WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, g_pane_main, (HMENU)id_btn_start, hInstance);

  mo_snprintf_wchar(
      buf, sizeof(buf) / sizeof(wchar_t), L"%1$hs", gettext("Path to %1$hs executable:"), gettext("Whisper"));
  g_lbl_exe_path = create_window(0, WC_STATICW, buf, WS_CHILD | WS_VISIBLE, g_pane_settings, NULL, hInstance);
  g_edt_exe_path = create_window(WS_EX_CLIENTEDGE,
                                 WC_EDITW,
                                 NULL,
                                 WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                                 g_pane_settings,
                                 (HMENU)id_edt_exe_path,
                                 hInstance);
  g_btn_exe_path = create_window(
      0, WC_BUTTONW, L"...", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, g_pane_settings, (HMENU)id_btn_exe_path, hInstance);
  g_lbl_exe_path_error = create_window(0, WC_STATICW, NULL, WS_CHILD | WS_VISIBLE, g_pane_settings, NULL, hInstance);
  mo_snprintf_wchar(buf, sizeof(buf) / sizeof(wchar_t), L"%1$hs", "%1$hs", gettext("Model directory:"));
  g_lbl_model_dir = create_window(0, WC_STATICW, buf, WS_CHILD | WS_VISIBLE, g_pane_settings, NULL, hInstance);
  g_edt_model_dir = create_window(WS_EX_CLIENTEDGE,
                                  WC_EDITW,
                                  NULL,
                                  WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                                  g_pane_settings,
                                  (HMENU)id_edt_model_dir,
                                  hInstance);
  g_btn_model_dir = create_window(0,
                                  WC_BUTTONW,
                                  L"...",
                                  WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                  g_pane_settings,
                                  (HMENU)id_btn_model_dir,
                                  hInstance);

  mo_snprintf_wchar(
      buf, sizeof(buf) / sizeof(wchar_t), L"%1$hs", gettext("Additional Arguments for %1$hs:"), gettext("Whisper"));
  g_lbl_additional_args = create_window(0, WC_STATICW, buf, WS_CHILD | WS_VISIBLE, g_pane_settings, NULL, hInstance);
  g_edt_additional_args = create_window(
      WS_EX_CLIENTEDGE, WC_EDITW, NULL, WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, g_pane_settings, NULL, hInstance);

  mo_snprintf_wchar(buf,
                    sizeof(buf) / sizeof(wchar_t),
                    L"",
                    gettext("Subtitle generation is internally divided into three steps.\r\n"
                            "You can run each step individually using the buttons on the right.\r\n"
                            "Note that if you run it from here, temporary files will not be automatically deleted."));
  g_lbl_description = create_window(WS_EX_CLIENTEDGE,
                                    WC_EDITW,
                                    buf,
                                    WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
                                    g_pane_advanced,
                                    NULL,
                                    hInstance);
  mo_snprintf_wchar(buf, sizeof(buf) / sizeof(wchar_t), L"%1$hs", "%1$hs", gettext("STEP1: Encode audio to *.opus"));
  g_btn_raw2opus = create_window(
      0, WC_BUTTONW, buf, WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, g_pane_advanced, (HMENU)id_btn_raw2opus, hInstance);
  mo_snprintf_wchar(
      buf, sizeof(buf) / sizeof(wchar_t), L"%1$hs", "%1$hs", gettext("STEP2: Generate *.json from *.opus"));
  g_btn_opus2json = create_window(
      0, WC_BUTTONW, buf, WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, g_pane_advanced, (HMENU)id_btn_opus2json, hInstance);
  mo_snprintf_wchar(buf, sizeof(buf) / sizeof(wchar_t), L"%1$hs", "%1$hs", gettext("STEP3: Convert *.json to *.exo"));
  g_btn_json2exo = create_window(
      0, WC_BUTTONW, buf, WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, g_pane_advanced, (HMENU)id_btn_json2exo, hInstance);
  mo_snprintf_wchar(buf, sizeof(buf) / sizeof(wchar_t), L"%1$hs", "%1$hs", gettext("Abort"));
  g_btn_abort = create_window(
      0, WC_BUTTONW, buf, WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, g_pane_advanced, (HMENU)id_btn_abort, hInstance);

  g_progress = create_window(0, PROGRESS_CLASSW, NULL, WS_CHILD | WS_VISIBLE | PBS_SMOOTH, window, NULL, hInstance);
  SendMessageW(g_progress, PBM_SETRANGE, 0, MAKELPARAM(0, 10000));
  g_logview = create_window(WS_EX_CLIENTEDGE,
                            WC_LISTBOXW,
                            NULL,
                            WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOINTEGRALHEIGHT | LBS_DISABLENOSCROLL,
                            window,
                            NULL,
                            hInstance);

  SYS_INFO si;
  HFONT font = NULL;
  if (!fp->exfunc->get_sys_info(fp, &si)) {
    ereport(emsg_i18nf(err_type_generic,
                       err_fail,
                       L"%1$hs",
                       gettext("Unable to get system information from %1$hs."),
                       gettext("AviUtl")));
    font = GetStockObject(DEFAULT_GUI_FONT);
  } else {
    font = si.hfont;
  }
  HWND handles[] = {
      window,
      g_tab,
      g_pane_main,
      g_pane_settings,
      g_pane_advanced,

      g_lbl_model,
      g_cmb_model,
      g_lbl_language,
      g_cmb_language,
      g_lbl_initial_prompt,
      g_edt_initial_prompt,
      g_lbl_insert_position,
      g_cmb_insert_position,
      g_lbl_insert_mode,
      g_cmb_insert_mode,
      g_lbl_module,
      g_cmb_module,

      g_btn_start,

      g_lbl_exe_path,
      g_edt_exe_path,
      g_btn_exe_path,
      g_lbl_exe_path_error,
      g_lbl_model_dir,
      g_edt_model_dir,
      g_btn_model_dir,
      g_lbl_additional_args,
      g_edt_additional_args,

      g_lbl_description,
      g_btn_raw2opus,
      g_btn_opus2json,
      g_btn_json2exo,
      g_btn_abort,

      g_progress,
  };
  for (size_t i = 0; i < sizeof(handles) / sizeof(handles[0]); i++) {
    SendMessageW(handles[i], WM_SETFONT, (WPARAM)font, MAKELPARAM(FALSE, 0));
  }
  SendMessageW(g_logview, WM_SETFONT, (WPARAM)(GetStockObject(SYSTEM_FIXED_FONT)), MAKELPARAM(FALSE, 0));

  SendMessageW(window, WM_SIZE, 0, 0);
}

static void filter_gui_resize(HWND const window, void *const editp, FILTER *const fp) {
  (void)editp;
  (void)fp;
  SYS_INFO si;
  TEXTMETRICW tm;
  {
    HFONT font = NULL;
    if (!fp->exfunc->get_sys_info(fp, &si)) {
      ereport(emsg_i18nf(err_type_generic,
                         err_fail,
                         L"%1$hs",
                         gettext("Unable to get system information from %1$hs."),
                         gettext("AviUtl")));
      font = GetStockObject(DEFAULT_GUI_FONT);
    } else {
      font = si.hfont;
    }
    HDC hdc = GetDC(window);
    HFONT old_font = (HFONT)SelectObject(hdc, font);
    GetTextMetricsW(hdc, &tm);
    SelectObject(hdc, old_font);
    ReleaseDC(window, hdc);
  }

  int padding = 4;

  RECT r;
  GetWindowRect(g_cmb_model, &r);
  int const item_height = r.bottom - r.top;

  RECT client;
  GetClientRect(window, &client);

  int tab_height;
  {
    RECT tabc = client;
    TabCtrl_AdjustRect(g_tab, FALSE, &tabc);
    int const margin = (client.bottom - client.top) - (tabc.bottom - tabc.top);
    tab_height = item_height * 3 + tm.tmHeight * 2 + padding * 2 + margin;
  }
  MoveWindow(g_tab, 0, 0, client.right - client.left, tab_height, TRUE);

  RECT tab = {.left = 0, .top = 0, .right = client.right - client.left, .bottom = tab_height};
  TabCtrl_AdjustRect(g_tab, FALSE, &tab);
  MoveWindow(g_pane_main, tab.left, tab.top, tab.right - tab.left, tab.bottom - tab.top, TRUE);
  MoveWindow(g_pane_settings, tab.left, tab.top, tab.right - tab.left, tab.bottom - tab.top, TRUE);
  MoveWindow(g_pane_advanced, tab.left, tab.top, tab.right - tab.left, tab.bottom - tab.top, TRUE);

  int const item_width4 = (tab.right - tab.left) / 4;
  int const item_width4_static = (600 - ((client.right - client.left) - (tab.right - tab.left))) / 4;

  int x, y;

  // Main tab
  x = 0;
  y = 0;
  MoveWindow(g_lbl_model, x, y, item_width4_static - padding, tm.tmHeight, TRUE);
  x += item_width4_static;
  MoveWindow(g_lbl_language, x, y, item_width4_static - padding, tm.tmHeight, TRUE);
  x += item_width4_static;
  MoveWindow(g_lbl_initial_prompt, x, y, (tab.right - tab.left) - item_width4_static * 2, tm.tmHeight, TRUE);
  x = 0;
  y += tm.tmHeight;
  MoveWindow(g_cmb_model, x, y, item_width4_static - padding, item_height * 8, TRUE);
  x += item_width4_static;
  MoveWindow(g_cmb_language, x, y, item_width4_static - padding, item_height * 8, TRUE);
  x += item_width4_static;
  MoveWindow(g_edt_initial_prompt, x, y, (tab.right - tab.left) - item_width4_static * 2, item_height, TRUE);

  y += item_height + padding;
  x = 0;
  MoveWindow(g_lbl_insert_position, x, y, item_width4_static - padding, tm.tmHeight, TRUE);
  x += item_width4_static;
  MoveWindow(g_lbl_insert_mode, x, y, item_width4_static - padding, tm.tmHeight, TRUE);
  x += item_width4_static;
  MoveWindow(g_lbl_module, x, y, (tab.right - tab.left) - item_width4_static * 2, tm.tmHeight, TRUE);
  x = 0;
  y += tm.tmHeight;
  MoveWindow(g_cmb_insert_position, x, y, item_width4_static - padding, item_height * 8, TRUE);
  x += item_width4_static;
  MoveWindow(g_cmb_insert_mode, x, y, item_width4_static - padding, item_height * 8, TRUE);
  x += item_width4_static;
  MoveWindow(g_cmb_module, x, y, (tab.right - tab.left) - item_width4_static * 2, item_height * 8, TRUE);

  x = (tab.right - tab.left) - item_width4_static;
  y += item_height + padding;
  MoveWindow(g_btn_start, x, y, item_width4_static, item_height, TRUE);

  // Global Settings tab
  x = 0;
  y = 0;
  MoveWindow(g_lbl_exe_path, x, y, item_width4 * 4, tm.tmHeight, TRUE);
  y += tm.tmHeight;
  MoveWindow(g_edt_exe_path, x, y, item_width4 * 4 - item_height * 2, item_height, TRUE);
  x += item_width4 * 4 - item_height * 2;
  MoveWindow(g_btn_exe_path, x, y, item_height * 2, item_height, TRUE);
  y += item_height;
  x = 0;
  MoveWindow(g_lbl_exe_path_error, x, y, item_width4 * 4, tm.tmHeight, TRUE);
  x = 0;
  y += tm.tmHeight + padding;
  MoveWindow(g_lbl_model_dir, x, y, item_width4 * 2, tm.tmHeight, TRUE);
  x += item_width4 * 2;
  MoveWindow(g_lbl_additional_args, x, y, item_width4 * 2, tm.tmHeight, TRUE);
  x = 0;
  y += tm.tmHeight;
  MoveWindow(g_edt_model_dir, x, y, item_width4 * 2 - item_height * 2 - padding, item_height, TRUE);
  x += item_width4 * 2 - item_height * 2 - padding;
  MoveWindow(g_btn_model_dir, x, y, item_height * 2 - padding, item_height, TRUE);
  x += item_height * 2;
  MoveWindow(g_edt_additional_args, x, y, item_width4 * 2, item_height, TRUE);

  // Advanced tab
  x = 0;
  y = 0;
  MoveWindow(g_lbl_description, x, y, item_width4 * 2 - padding, tab.bottom - tab.top, TRUE);
  x += item_width4 * 2;
  MoveWindow(g_btn_raw2opus, x, y, item_width4 * 2, item_height, TRUE);
  y += item_height + (tab.bottom - tab.top - item_height * 3) / 2;
  MoveWindow(g_btn_opus2json, x, y, item_width4 * 2, item_height, TRUE);
  y += item_height + (tab.bottom - tab.top - item_height * 3) / 2;
  MoveWindow(g_btn_json2exo, x, y, item_width4 * 2, item_height, TRUE);
  y += item_height + padding * 2;
  MoveWindow(g_btn_abort, x, y, item_width4 * 2, item_height, TRUE);

  // Progress and Log
  x = padding;
  y = tab_height;
  MoveWindow(g_progress, x, y, (client.right - client.left) - padding * 2, 8, TRUE);
  y += 8;
  MoveWindow(
      g_logview, x, y, (client.right - client.left) - padding * 2, (client.bottom - client.top) - y - padding, TRUE);
}

static void filter_activate(HWND const window, void *const editp, FILTER *const fp) {
  (void)window;
  (void)editp;
  (void)fp;
  error err = eok();
  wchar_t *buf = NULL;
  if (!g_modules) {
    wchar_t const *const module = config_get_module(processor_get_config(g_processor));
    err = processor_get_modules(g_processor, &g_modules);
    if (efailed(err)) {
      err = ethru(err);
      goto cleanup;
    }
    SendMessageW(g_cmb_module, CB_RESETCONTENT, 0, 0);
    size_t const n = OV_ARRAY_LENGTH(g_modules);
    for (size_t i = 0; i < n; ++i) {
      err = OV_ARRAY_GROW(&buf, wcslen(g_modules[i].name) + wcslen(g_modules[i].description) + 4);
      if (efailed(err)) {
        err = ethru(err);
        goto cleanup;
      }
      wcscpy(buf, g_modules[i].name);
      wcscat(buf, L" - ");
      wcscat(buf, g_modules[i].description);
      SendMessageW(g_cmb_module, CB_ADDSTRING, 0, (LPARAM)buf);
      if (wcscmp(g_modules[i].module, module) == 0) {
        SendMessageW(g_cmb_module, CB_SETCURSEL, i, 0);
      }
    }
  }
cleanup:
  OV_ARRAY_DESTROY(&buf);
  ereport(err);
}

static bool filter_init(HWND const window, void *const editp, FILTER *const fp) {
  (void)window;
  mtx_init(&g_mtx, mtx_plain);
  cnd_init(&g_cnd);
  g_gui_thread_id = GetCurrentThreadId();
  error err = mo_parse_from_resource(&g_mp, get_hinstance());
  if (efailed(err)) {
    ereport(err);
  } else {
    mo_set_default(g_mp);
  }
  filter_gui_init(window, editp, fp);
  err = aviutl_init();
  if (efailed(err)) {
    err = ethru(err);
    goto cleanup;
  }
  err = processor_create(&g_processor,
                         &(struct processor_params){
                             .hinst = get_hinstance(),
                             .editp = editp,
                             .fp = fp,
                             .userdata = NULL,
                             .on_next_task = on_next_task,
                             .on_progress = on_progress,
                             .on_log_line = on_log_line,
                             .on_create_exo = on_create_exo,
                             .on_finish = on_finish,
                         });
  if (efailed(err)) {
    err = ethru(err);
    goto cleanup;
  }

  struct config *cfg = processor_get_config(g_processor);
  wchar_t const *s = config_get_whisper_path(cfg);
  SetWindowTextW(g_edt_exe_path, s && s[0] != L'\0' ? s : L"");
  update_exe_path();
  // If an error message is displayed, there is an error in the exe path setting.
  // In this case, switch to the settings tab.
  TabCtrl_SetCurSel(g_tab, GetWindowTextLengthW(g_lbl_exe_path_error) > 0 ? 1 : 0);
  SendMessageW(
      window, WM_NOTIFY, id_tab, (LPARAM) & (NMHDR){.code = TCN_SELCHANGE, .idFrom = id_tab, .hwndFrom = g_tab});
  s = config_get_model_dir(cfg);
  SetWindowTextW(g_edt_model_dir, s && s[0] != L'\0' ? s : L"");
  s = config_get_additional_args(cfg);
  SetWindowTextW(g_edt_additional_args, s && s[0] != L'\0' ? s : L"");
cleanup:
  if (efailed(err)) {
    error_to_log(err);
    update_state(gui_state_invalid);
    return false;
  }
  return true;
}

static void filter_exit(HWND const window, void *const editp, FILTER *const fp) {
  (void)window;
  (void)editp;
  (void)fp;
  if (g_pane_main != NULL) {
    RemoveWindowSubclass(g_pane_main, subclass_proc, (UINT_PTR)subclass_proc);
  }
  if (g_pane_settings != NULL) {
    RemoveWindowSubclass(g_pane_settings, subclass_proc, (UINT_PTR)subclass_proc);
  }
  if (g_pane_advanced != NULL) {
    RemoveWindowSubclass(g_pane_advanced, subclass_proc, (UINT_PTR)subclass_proc);
  }

  if (g_modules) {
    processor_module_destroy(&g_modules);
  }
  if (g_processor) {
    processor_destroy(&g_processor);
  }
  if (g_log_buffer) {
    OV_ARRAY_DESTROY(&g_log_buffer);
  }
  ereport(aviutl_exit());
  mo_set_default(NULL);
  mo_free(&g_mp);
  cnd_destroy(&g_cnd);
  mtx_destroy(&g_mtx);
}

static BOOL filter_wndproc(HWND const window,
                           UINT const message,
                           WPARAM const wparam,
                           LPARAM const lparam,
                           void *const editp,
                           FILTER *const fp) {
  (void)wparam;
  aviutl_set_pointers(fp, editp);
  switch (message) {
  case WM_FILTER_INIT:
    if (!filter_init(window, editp, fp)) {
      return FALSE;
    }
    break;
  case WM_FILTER_EXIT:
    filter_exit(window, editp, fp);
    break;
  case WM_FILTER_FILE_OPEN:
    if (aviutl_initalized()) {
      reset_gui();
      update_state(gui_state_ready);
    }
    break;
  case WM_FILTER_FILE_CLOSE:
    if (aviutl_initalized()) {
      reset_gui();
      update_state(gui_state_no_project);
    }
    break;
  case WM_ACTIVATE:
    if (aviutl_initalized()) {
      filter_activate(window, editp, fp);
    }
    break;
  case WM_NOTIFY: {
    NMHDR *hdr = (NMHDR *)lparam;
    if (hdr->idFrom == id_tab && hdr->code == TCN_SELCHANGE) {
      int const index = TabCtrl_GetCurSel(g_tab);
      ShowWindow(g_pane_main, index == 0 ? SW_SHOW : SW_HIDE);
      ShowWindow(g_pane_settings, index == 1 ? SW_SHOW : SW_HIDE);
      ShowWindow(g_pane_advanced, index == 2 ? SW_SHOW : SW_HIDE);
    }
  } break;
  case WM_TIMER:
    if (wparam == id_tmr_progress) {
      update_title();
    }
    break;
  case WM_SIZE:
    filter_gui_resize(window, editp, fp);
    break;
  case WM_GETMINMAXINFO: {
    MINMAXINFO *pmmi = (MINMAXINFO *)lparam;
    pmmi->ptMinTrackSize.x = 600;
    pmmi->ptMinTrackSize.y = 360;
  } break;
  case WM_PROCESS_LOG_LINE:
    add_log((wchar_t *)lparam);
    mtx_lock(&g_mtx);
    g_log_processed = true;
    cnd_signal(&g_cnd);
    mtx_unlock(&g_mtx);
    break;
  case WM_PROCESS_FINISHED:
    finish((void *)wparam, (error)lparam);
    break;
  case WM_PROCESS_NEXT_TASK:
    mtx_lock(&g_mtx);
    ULONGLONG const now = GetTickCount64();
    g_progress_info = (struct progress){
        .type = (enum processor_type)lparam,
        .started_at = now,
        .last_updated_at = now,
        .prev_updated_at = now,
    };
    mtx_unlock(&g_mtx);
    update_title();
    break;
  case WM_PROCESS_PROGRESS:
    update_title();
    break;
  case WM_PROCESS_CREATE_EXO: {
    create_exo((void *)wparam, (struct processor_exo_info *)lparam);
    mtx_lock(&g_mtx);
    g_exo_processed = true;
    cnd_signal(&g_cnd);
    mtx_unlock(&g_mtx);
  } break;
  case WM_PROCESS_UPDATED:
    return TRUE;
  default:
    break;
  }
  return FALSE;
}

static BOOL filter_project_save(FILTER *const fp, void *const editp, void *const data, int *const size) {
  if (!aviutl_initalized()) {
    return false;
  }
  (void)fp;
  (void)editp;
  struct config *cfg = NULL;
  char *buf = NULL;
  error err = config_create(&cfg);
  if (efailed(err)) {
    err = ethru(err);
    goto cleanup;
  }
  err = gui_to_config(cfg);
  if (efailed(err)) {
    err = ethru(err);
    goto cleanup;
  }
  size_t sz;
  err = config_save(cfg, &buf, &sz);
  if (efailed(err)) {
    err = ethru(err);
    goto cleanup;
  }
  if (data) {
    memcpy(data, buf, sz);
  }
  if (size) {
    *size = (int)sz;
  }
cleanup:
  if (buf) {
    ereport(mem_free(&buf));
  }
  if (cfg) {
    config_destroy(&cfg);
  }
  if (efailed(err)) {
    ereport(err);
    return FALSE;
  }
  return TRUE;
}

static BOOL filter_project_load(FILTER *const fp, void *const editp, void *const data, int const size) {
  if (!aviutl_initalized()) {
    return false;
  }
  (void)fp;
  (void)editp;
  struct config *cfg = NULL;
  error err = config_create(&cfg);
  if (efailed(err)) {
    err = ethru(err);
    goto cleanup;
  }
  err = config_load(cfg, data, (size_t)size);
  if (efailed(err)) {
    err = ethru(err);
    goto cleanup;
  }
  err = config_to_gui(cfg, true);
  if (efailed(err)) {
    err = ethru(err);
    goto cleanup;
  }
cleanup:
  if (cfg) {
    config_destroy(&cfg);
  }
  if (efailed(err)) {
    ereport(err);
    return FALSE;
  }
  return TRUE;
}

FILTER_DLL __declspec(dllexport) * *__stdcall GetFilterTableList(void);
FILTER_DLL __declspec(dllexport) * *__stdcall GetFilterTableList(void) {
  static char name[64];
  static char information[128];
  static FILTER_DLL filter = {
      .flag = FILTER_FLAG_ALWAYS_ACTIVE | FILTER_FLAG_EX_INFORMATION | FILTER_FLAG_DISP_FILTER |
              FILTER_FLAG_WINDOW_SIZE | FILTER_FLAG_WINDOW_THICKFRAME,
      .x = 600,
      .y = 360,
      .func_WndProc = filter_wndproc,
      .func_project_load = filter_project_load,
      .func_project_save = filter_project_save,
  };
  static FILTER_DLL *filter_list[] = {
      &filter,
      NULL,
  };
  if (filter.name == NULL) {
    ov_snprintf(name, sizeof(name), NULL, "%s", gettext("Subtitler"));
    ov_snprintf(information, sizeof(information), NULL, "%s %s", name, VERSION);
    filter.name = name;
    filter.information = information;
  }
  return (FILTER_DLL **)&filter_list;
}

static NODISCARD error get_generic_message(int const type, int const code, struct NATIVE_STR *const dest) {
  if (!dest) {
    return errg(err_invalid_arugment);
  }
  if (type != err_type_generic) {
    dest->len = 0;
    return eok();
  }
  switch (code) {
  case err_fail:
    return to_wstr(&str_unmanaged_const(gettext("Failed.")), dest);
  case err_unexpected:
    return to_wstr(&str_unmanaged_const(gettext("Unexpected.")), dest);
  case err_invalid_arugment:
    return to_wstr(&str_unmanaged_const(gettext("Invalid argument.")), dest);
  case err_null_pointer:
    return to_wstr(&str_unmanaged_const(gettext("NULL pointer.")), dest);
  case err_out_of_memory:
    return to_wstr(&str_unmanaged_const(gettext("Out of memory.")), dest);
  case err_not_sufficient_buffer:
    return to_wstr(&str_unmanaged_const(gettext("Not sufficient buffer.")), dest);
  case err_not_found:
    return to_wstr(&str_unmanaged_const(gettext("Not found.")), dest);
  case err_abort:
    return to_wstr(&str_unmanaged_const(gettext("Aborted.")), dest);
  case err_not_implemented_yet:
    return to_wstr(&str_unmanaged_const(gettext("Not implemented yet.")), dest);
  case err_lua:
    return to_wstr(&str_unmanaged_const(gettext("An error occurred in Lua script.")), dest);
  }
  return to_wstr(&str_unmanaged_const(gettext("Unknown error code.")), dest);
}

static NODISCARD error get_error_message(int const type, int const code, struct NATIVE_STR *const dest) {
  if (type == err_type_generic) {
    return get_generic_message(type, code, dest);
  }
  if (type == err_type_hresult) {
    return error_win32_message_mapper(type, code, MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL), dest);
  }
  return scpy(dest, NSTR("Unknown error code."));
}

static void
error_reporter(error const e, struct NATIVE_STR const *const message, struct ov_filepos const *const filepos) {
  struct NATIVE_STR tmp = {0};
  struct NATIVE_STR msg = {0};
  error err = error_to_string(e, &tmp);
  if (efailed(err)) {
    err = ethru(err);
    goto cleanup;
  }
  NATIVE_CHAR buf[1024] = {0};
  wsprintfW(buf, NSTR("\r\n(reported at %hs:%ld %hs())\r\n"), filepos->file, filepos->line, filepos->func);
  err = scpym(&msg, message->ptr, buf, tmp.ptr);
  if (efailed(err)) {
    err = ethru(err);
    goto cleanup;
  }
  OutputDebugStringW(msg.ptr);

cleanup:
  if (efailed(err)) {
    OutputDebugStringW(NSTR("Unable to report error"));
    efree(&err);
  }
  eignore(sfree(&msg));
  eignore(sfree(&tmp));
}

BOOL APIENTRY DllMain(HINSTANCE const inst, DWORD const reason, LPVOID const reserved);
BOOL APIENTRY DllMain(HINSTANCE const inst, DWORD const reason, LPVOID const reserved) {
  // trans: This dagger helps UTF-8 detection. You don't need to translate this.
  (void)gettext_noop("†");
  (void)reserved;
  switch (reason) {
  case DLL_PROCESS_ATTACH:
    ov_init();
    error_set_message_mapper(get_error_message);
    error_set_reporter(error_reporter);
    set_hinstance(inst);
    break;
  case DLL_PROCESS_DETACH:
    ov_exit();
    break;
  default:
    break;
  }
  return TRUE;
}
