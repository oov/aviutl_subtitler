#include "opus2json.h"

#include <ovarray.h>
#include <ovnum.h>
#include <ovprintf.h>
#include <ovthreads.h>
#include <ovutf.h>
#include <ovutil/win32.h>

#ifdef __GNUC__
#  ifndef __has_warning
#    define __has_warning(x) 0
#  endif
#  pragma GCC diagnostic push
#  if __has_warning("-Wreserved-macro-identifier")
#    pragma GCC diagnostic ignored "-Wreserved-macro-identifier"
#  endif
#endif // __GNUC__
#include <opusfile.h>
#ifdef __GNUC__
#  pragma GCC diagnostic pop
#endif // __GNUC__

#include "i18n.h"
#include "path.h"
#include "process.h"

struct opus2json_context {
  struct process_line_buffer_context out_buffer;
  struct process_line_buffer_context err_buffer;
  struct opus2json_params params;
  int64_t samples;
  int sample_rate;
  int channels;
  bool abort_requested;
  mtx_t mtx;
  struct process *pr;
  wchar_t *json_path;
  wchar_t *buffer;
};

static int opus_read(void *_stream, unsigned char *_ptr, int _nbytes) {
  DWORD read;
  if (!ReadFile(_stream, _ptr, (DWORD)_nbytes, &read, NULL)) {
    return -1;
  }
  return (int)read;
}

static int opus_seek(void *_stream, opus_int64 _offset, int _whence) {
  DWORD method;
  switch (_whence) {
  case SEEK_SET:
    method = FILE_BEGIN;
    break;
  case SEEK_CUR:
    method = FILE_CURRENT;
    break;
  case SEEK_END:
    method = FILE_END;
    break;
  default:
    return -1;
  }
  LARGE_INTEGER li;
  li.QuadPart = _offset;
  if (!SetFilePointerEx(_stream, li, NULL, method)) {
    return -1;
  }
  return 0;
}

static opus_int64 opus_tell(void *_stream) {
  LARGE_INTEGER li;
  li.QuadPart = 0;
  if (!SetFilePointerEx(_stream, li, &li, FILE_CURRENT)) {
    return -1;
  }
  return li.QuadPart;
}

static int opus_close(void *_stream) {
  CloseHandle(_stream);
  return 0;
}

static NODISCARD error get_opus_info(wchar_t const *const opus_path, int64_t *const samples, int *const channels) {
  if (!opus_path) {
    return errg(err_null_pointer);
  }
  error err = eok();
  OggOpusFile *of = NULL;
  HANDLE h = CreateFileW(opus_path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
  if (h == INVALID_HANDLE_VALUE) {
    HRESULT hr = HRESULT_FROM_WIN32(GetLastError());
    if (hr == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND)) {
      err =
          emsg_i18nf(err_type_generic, err_not_found, L"%1$ls", gettext("The file \"%1$ls\" is not found."), opus_path);
    } else {
      err = errhr(hr);
    }
    goto cleanup;
  }
  int er;
  of = op_open_callbacks(h,
                         &(OpusFileCallbacks){
                             .read = opus_read,
                             .seek = opus_seek,
                             .tell = opus_tell,
                             .close = opus_close,
                         },
                         NULL,
                         0,
                         &er);
  if (!of) {
    err = emsg_i18nf(err_type_generic, err_fail, L"", "%1$hs", gettext("Unable to open the file."));
    goto cleanup;
  }
  h = INVALID_HANDLE_VALUE;

  int const ch = op_channel_count(of, -1);
  int64_t const smp = op_pcm_total(of, -1);
  if (ch <= 0 || smp <= 0) {
    err = emsg_i18n(err_type_generic, err_fail, gettext("The Opus file is invalid."));
    goto cleanup;
  }
  if (channels) {
    *channels = ch;
  }
  if (samples) {
    *samples = smp;
  }
cleanup:
  if (of) {
    op_free(of);
    of = NULL;
  }
  if (h != INVALID_HANDLE_VALUE) {
    CloseHandle(h);
  }
  return err;
}

static NODISCARD error make_json_path(wchar_t const *const opus_path, wchar_t **const json_path) {
  if (!opus_path) {
    return errg(err_invalid_arugment);
  }
  wchar_t const *const before = path_extract_file_name(opus_path);
  if (!before) {
    return errg(err_fail);
  }
  error err = eok();
  size_t const before_len = wcslen(before);
  wchar_t buf[256];
  wchar_t *filename = NULL;
  static wchar_t const json_ext[] = L".json";

  if (before_len + sizeof(json_ext) / sizeof(wchar_t) < sizeof(buf) / sizeof(wchar_t)) {
    filename = buf;
  } else {
    err = OV_ARRAY_GROW(&filename, before_len + sizeof(json_ext) / sizeof(wchar_t));
    if (efailed(err)) {
      err = ethru(err);
      goto cleanup;
    }
  }
  wcscpy(filename, before);
  wchar_t *ext = wcsrchr(filename, L'.');
  if (!ext) {
    ext = filename + before_len;
  }
  wcscpy(ext, json_ext);
  err = path_get_temp_file(json_path, filename);
  if (efailed(err)) {
    err = ethru(err);
    goto cleanup;
  }
cleanup:
  if (filename != NULL && filename != buf) {
    OV_ARRAY_DESTROY(&filename);
  }
  return err;
}

static size_t mystrnlen(char const *s, size_t const maxlen) {
  size_t len = 0;
  while (len < maxlen && *s++) {
    ++len;
  }
  return len;
}

static int64_t parse_time_code(char const *const time_code) {
  if (!time_code) {
    return -1;
  }
  size_t const len = mystrnlen(time_code, 12);
  if (len >= 12 && isdigit(time_code[0]) && isdigit(time_code[1]) && time_code[2] == ':' && isdigit(time_code[3]) &&
      isdigit(time_code[4]) && time_code[5] == ':' && isdigit(time_code[6]) && isdigit(time_code[7]) &&
      time_code[8] == '.' && isdigit(time_code[9]) && isdigit(time_code[10]) && isdigit(time_code[11])) {
    // 00:00:00.000
    int const hours = (time_code[0] - '0') * 10 + (time_code[1] - '0');
    int const minutes = (time_code[3] - '0') * 10 + (time_code[4] - '0');
    int const seconds = (time_code[6] - '0') * 10 + (time_code[7] - '0');
    int const milliseconds = (time_code[9] - '0') * 100 + (time_code[10] - '0') * 10 + (time_code[11] - '0');
    return (int64_t)(hours * 3600000) + (int64_t)(minutes * 60000 + seconds * 1000 + milliseconds);
  } else if (len >= 9 && isdigit(time_code[0]) && isdigit(time_code[1]) && time_code[2] == ':' &&
             isdigit(time_code[3]) && isdigit(time_code[4]) && time_code[5] == '.' && isdigit(time_code[6]) &&
             isdigit(time_code[7]) && isdigit(time_code[8])) {
    // 00:00.000
    int const minutes = (time_code[0] - '0') * 10 + (time_code[1] - '0');
    int const seconds = (time_code[3] - '0') * 10 + (time_code[4] - '0');
    int const milliseconds = (time_code[6] - '0') * 100 + (time_code[7] - '0') * 10 + (time_code[8] - '0');
    return (int64_t)(minutes * 60000 + seconds * 1000 + milliseconds);
  }
  return -1;
}

static void process_line(void *const userdata, char const *const message) {
  struct opus2json_context *const ctx = userdata;
  if (ctx->params.on_progress) {
    char const *arrow = strstr(message, " --> ");
    if (arrow != NULL && message[0] == '[' && arrow[5] != '\0') {
      // Read a line like [00:00.000 --> 00:00.000] or [00:00:00.000 --> 00:00:00.000] and report progress
      int64_t s = parse_time_code(message + 1);
      int64_t e = parse_time_code(arrow + 5);
      if (s != -1 && s != -1 && s <= e) {
        int64_t const total = ctx->samples * 1000 / ctx->sample_rate;
        ctx->params.on_progress(ctx->params.userdata, (int)((e * 10000) / total));
      }
    } else if (strstr(message, "audio seconds/s") != NULL && message[3] == '%') {
      // Read a line like " 87% | 20/23 | 00:06<<00:00 |  3.11 audio seconds/s" and report progress
      char const *p = message;
      while (*p && !isdigit(*p)) {
        ++p;
      }
      int64_t progress = 0;
      if (ov_atoi(p, &progress, false)) {
        ctx->params.on_progress(ctx->params.userdata, (int)(progress * 100));
      }
    }
  }

  if (ctx->params.on_log_line) {
    size_t const msglen = strlen(message);
    UINT const cp = msglen && ov_utf8_to_wchar_len(message, msglen) == 0 ? CP_ACP : CP_UTF8;
    int len = MultiByteToWideChar(cp, 0, message, -1, NULL, 0);
    if (len == 0) {
      ereport(errhr(HRESULT_FROM_WIN32(GetLastError())));
      return;
    }
    error err = OV_ARRAY_GROW(&ctx->buffer, len);
    if (efailed(err)) {
      ereport(err);
      return;
    }
    if (MultiByteToWideChar(cp, 0, message, -1, ctx->buffer, len) == 0) {
      ereport(errhr(HRESULT_FROM_WIN32(GetLastError())));
      return;
    }
    ctx->params.on_log_line(ctx->params.userdata, ctx->buffer);
  }
}

static void process_on_receive_stdout(void *userdata, void const *const ptr, size_t const len) {
  struct opus2json_context *const ctx = userdata;
  process_line_buffer(&ctx->out_buffer, ptr, len);
}

static void process_on_receive_stderr(void *userdata, void const *const ptr, size_t const len) {
  struct opus2json_context *const ctx = userdata;
  process_line_buffer(&ctx->err_buffer, ptr, len);
}

static void process_on_close_stdout(void *userdata, error err) {
  struct opus2json_context *const ctx = userdata;
  HANDLE h = INVALID_HANDLE_VALUE;

  mtx_lock(&ctx->mtx);

  if (efailed(err)) {
    goto cleanup;
  }
  if (ctx->abort_requested) {
    err = errg(err_abort);
    goto cleanup;
  }
  h = CreateFileW(ctx->json_path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
  if (h == INVALID_HANDLE_VALUE) {
    err = errhr(HRESULT_FROM_WIN32(GetLastError()));
    goto cleanup;
  }
cleanup:
  if (h != INVALID_HANDLE_VALUE) {
    CloseHandle(h);
    h = INVALID_HANDLE_VALUE;
  }
  if (efailed(err)) {
    DeleteFileW(ctx->json_path);
  }
  void (*on_finish)(void *const userdata, error err) = ctx->params.on_finish;
  void *ud = ctx->params.userdata;
  mtx_unlock(&ctx->mtx);
  if (on_finish) {
    on_finish(ud, err);
  }
}

NODISCARD error opus2json_create(struct opus2json_context **const ctxpp, struct opus2json_params const *const params) {
  if (!ctxpp || *ctxpp || !params || !params->opus_path || !params->whisper_path) {
    return errg(err_invalid_arugment);
  }

  struct opus2json_context *ctx = NULL;
  struct process *pr = NULL;
  wchar_t *json_path = NULL;
  wchar_t *temp_path = NULL;
  int channels;
  int64_t samples;

  error err = get_opus_info(params->opus_path, &samples, &channels);
  if (efailed(err)) {
    err = ethru(err);
    goto cleanup;
  }
  err = make_json_path(params->opus_path, &json_path);
  if (efailed(err)) {
    err = ethru(err);
    goto cleanup;
  }
  DeleteFileW(json_path);

  err = path_get_temp_file(&temp_path, L"");
  if (efailed(err)) {
    err = ethru(err);
    goto cleanup;
  }
  size_t const temp_path_len = wcslen(temp_path);
  if (temp_path_len > 0 && (temp_path[temp_path_len - 1] == L'\\' || temp_path[temp_path_len - 1] == L'/')) {
    temp_path[temp_path_len - 1] = L'\0';
  }

  err = mem(&ctx, 1, sizeof(struct opus2json_context));
  if (efailed(err)) {
    err = ethru(err);
    goto cleanup;
  }
  *ctx = (struct opus2json_context){
      .params = *params,
  };
  mtx_init(&ctx->mtx, mtx_plain);
  mtx_lock(&ctx->mtx);

  wchar_t buf[4096];
  mo_snprintf_wchar(buf,
                    sizeof(buf) / sizeof(wchar_t),
                    L"%1$hs",
                    gettext("Generating *json from *.opus using %1$hs..."),
                    gettext("Whisper"));
  if (params->on_log_line) {
    params->on_log_line(params->userdata, buf);
  }
  ov_snprintf(buf,
              sizeof(buf),
              NULL,
              L"\"%ls\" \"%ls\" --output_dir \"%ls\" --output_format json --word_timestamps True %ls",
              params->whisper_path,
              params->opus_path,
              temp_path,
              params->additional_args);
  if (params->on_log_line) {
    params->on_log_line(params->userdata, buf);
  }

  err = process_create(&pr,
                       &(struct process_options){
                           .cmdline = buf,
                           .userdata = ctx,
                           .on_receive_stdout = process_on_receive_stdout,
                           .on_receive_stderr = process_on_receive_stderr,
                           .on_close_stdout = process_on_close_stdout,
                       });
  if (efailed(err)) {
    err = ethru(err);
    goto cleanup;
  }

  ctx->out_buffer.userdata = ctx;
  ctx->out_buffer.on_line = process_line;
  ctx->err_buffer.userdata = ctx;
  ctx->err_buffer.on_line = process_line;
  ctx->samples = samples;
  ctx->sample_rate = 48000;
  ctx->channels = channels;
  ctx->pr = pr;
  ctx->json_path = json_path;
  pr = NULL;
  json_path = NULL;
  mtx_unlock(&ctx->mtx);
  *ctxpp = ctx;
  ctx = NULL;
cleanup:
  if (pr) {
    process_destroy(&pr);
  }
  if (temp_path) {
    OV_ARRAY_DESTROY(&temp_path);
  }
  if (json_path) {
    OV_ARRAY_DESTROY(&json_path);
  }
  if (ctx) {
    mtx_unlock(&ctx->mtx);
    mtx_destroy(&ctx->mtx);
    ereport(mem_free(&ctx));
  }
  return err;
}

void opus2json_destroy(struct opus2json_context **const ctxpp) {
  if (!ctxpp || !*ctxpp) {
    return;
  }
  struct opus2json_context *ctx = *ctxpp;
  mtx_lock(&ctx->mtx);
  struct process *pr = ctx->pr;
  mtx_unlock(&ctx->mtx);
  if (pr) {
    process_destroy(&pr);
    pr = NULL;
  }
  mtx_lock(&ctx->mtx);
  ctx->pr = NULL;
  if (ctx->json_path) {
    OV_ARRAY_DESTROY(&ctx->json_path);
  }
  if (ctx->buffer) {
    OV_ARRAY_DESTROY(&ctx->buffer);
  }
  mtx_destroy(&ctx->mtx);
  ereport(mem_free(ctxpp));
}

NODISCARD error opus2json_abort(struct opus2json_context *const ctx) {
  error err = eok();
  mtx_lock(&ctx->mtx);
  if (!ctx->pr) {
    goto cleanup;
  }
  err = process_send_ctrl_break(ctx->pr);
  if (efailed(err)) {
    err = ethru(err);
    goto cleanup;
  }
  ctx->abort_requested = true;
cleanup:
  mtx_unlock(&ctx->mtx);
  return err;
}
