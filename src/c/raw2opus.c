#include "raw2opus.h"

#include <ovarray.h>
#include <ovprintf.h>
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
#include <opusenc.h>
#ifdef __GNUC__
#  pragma GCC diagnostic pop
#endif // __GNUC__

#include "aviutl.h"
#include "export_audio.h"
#include "i18n.h"

static size_t bytes_to_human_readable(char *const buf8, uint64_t const bytes, char const decimal_point) {
  size_t suffix = 0;
  int d, f;
  {
    uint64_t d64 = bytes, prev = 0;
    while (d64 >= 1000) {
      prev = d64;
      d64 /= 1024;
      suffix++;
    }
    d = (int)(d64);
    f = (int)(suffix ? prev - d64 * 1024 : prev);
  }
  size_t i = 0;
  if (!suffix) {
    if (d >= 100) {
      buf8[i++] = (char)(d / 100) + '0';
    }
    if (d >= 10) {
      buf8[i++] = (char)(d / 10 % 10) + '0';
    }
    buf8[i++] = (char)(d % 10) + '0';
  } else if (d >= 100) {
    d += (f + 512) / 1024;
    if (d >= 100) {
      buf8[i++] = (char)(d / 100) + '0';
      d %= 100;
    }
    buf8[i++] = (char)(d / 10) + '0';
    buf8[i++] = (char)(d % 10) + '0';
  } else if (d >= 10) {
    f = (f * 10 + 512) / 1024;
    buf8[i++] = (char)(d / 10) + '0';
    buf8[i++] = (char)(d % 10) + '0';
    buf8[i++] = decimal_point;
    buf8[i++] = (char)(f) + '0';
  } else {
    f = (f * 100 + 512) / 1024;
    buf8[i++] = (char)(d) + '0';
    buf8[i++] = decimal_point;
    buf8[i++] = (char)(f / 10) + '0';
    buf8[i++] = (char)(f % 10) + '0';
  }
  buf8[i] = '\0';
  return suffix;
}

struct raw2opus_context {
  struct raw2opus_params const *const params;
  size_t samples;
  HANDLE dest;
  OggOpusEnc *enc;
  error err;
};

static int opus_write(void *userdata, const unsigned char *ptr, opus_int32 len) {
  struct raw2opus_context *const ctx = userdata;
  error err = eok();
  if (ctx->err) {
    goto cleanup;
  }
  DWORD written;
  if (!WriteFile(ctx->dest, ptr, (DWORD)len, &written, NULL)) {
    err = errhr(HRESULT_FROM_WIN32(GetLastError()));
    goto cleanup;
  }
  if (written != (DWORD)len) {
    err = emsg_i18n(err_type_generic, err_fail, gettext("Unable to write the entire file."));
    goto cleanup;
  }
cleanup:
  if (efailed(err)) {
    if (efailed(ctx->err)) {
      efree(&err);
    } else {
      ctx->err = err;
      err = eok();
    }
  }
  return efailed(ctx->err) ? 1 : 0;
}

static int opus_close(void *userdata) {
  (void)userdata;
  return 0;
}

static bool export_audio_read(void *const userdata, void *const p, size_t const samples, int const progress) {
  struct raw2opus_context *const ctx = userdata;
  error err = eok();
  if (ctx->err) {
    goto cleanup;
  }
  int const r = ope_encoder_write(ctx->enc, p, (int)samples);
  if (r < 0) {
    err = emsg_i18nf(err_type_generic, err_fail, L"%1$hs", gettext("Unable to write file: %1$hs"), ope_strerror(r));
    goto cleanup;
  }
  ctx->samples += samples;
  if (ctx->params->on_progress) {
    if (!ctx->params->on_progress(ctx->params->userdata, progress)) {
      err = errg(err_abort);
      goto cleanup;
    }
  }
cleanup:
  if (efailed(err)) {
    if (efailed(ctx->err)) {
      efree(&err);
    } else {
      ctx->err = err;
      err = eok();
    }
  }
  return esucceeded(ctx->err);
}

NODISCARD error raw2opus(struct raw2opus_params const *const params, struct raw2opus_info *const info) {
  if (!params || !params->fp || !params->editp || !params->opus_path || !info) {
    return errg(err_invalid_arugment);
  }
  FILTER *fp = params->fp;
  FILE_INFO fi = {0};
  HANDLE dest = INVALID_HANDLE_VALUE;
  OggOpusEnc *enc = NULL;
  OggOpusComments *comments = NULL;
  error err = eok();

  if (!fp->exfunc->get_file_info(params->editp, &fi)) {
    err = emsg_i18n(err_type_generic, err_fail, gettext("Unable to retrieve file information."));
    goto cleanup;
  }

  if (params->on_log_line) {
    wchar_t msg[1024];
    mo_snprintf_wchar(msg, sizeof(msg) / sizeof(msg[0]), L"%1$ls", "Destination: %1$ls", params->opus_path);
    params->on_log_line(params->userdata, msg);
  }

  dest = CreateFileW(params->opus_path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
  if (dest == INVALID_HANDLE_VALUE) {
    err = errhr(HRESULT_FROM_WIN32(GetLastError()));
    goto cleanup;
  }

  struct raw2opus_context ctx = {
      .params = params,
      .dest = dest,
      .err = eok(),
  };

  comments = ope_comments_create();
  if (!comments) {
    err = emsg_i18n(err_type_generic, err_fail, gettext("Unable to create ogg opus comments."));
    goto cleanup;
  }
  int er;
  enc = ope_encoder_create_callbacks(
      &(OpusEncCallbacks){
          .write = opus_write,
          .close = opus_close,
      },
      &ctx,
      comments,
      fi.audio_rate,
      fi.audio_ch,
      0,
      &er);
  if (!enc) {
    err =
        emsg_i18nf(err_type_generic, err_fail, L"%1$hs", gettext("Unable to create encoder: %1$hs"), ope_strerror(er));
    goto cleanup;
  }

  ctx.enc = enc;
  err = export_audio(&(struct export_audio_params){
      .editp = params->editp,
      .fp = params->fp,
      .userdata = &ctx,
      .on_read = export_audio_read,
  });
  if (efailed(err)) {
    if (efailed(ctx.err)) {
      efree(&err);
      err = ctx.err;
      ctx.err = eok();
    }
    err = ethru(err);
    goto cleanup;
  }

  er = ope_encoder_drain(enc);
  if (er < 0) {
    err = emsg_i18nf(err_type_generic, err_fail, L"%1$hs", gettext("Unable to drain: %1$hs"), ope_strerror(er));
    goto cleanup;
  }

  if (params->on_log_line) {
    LARGE_INTEGER size;
    char b[8];
    wchar_t msg[1024];
    if (!GetFileSizeEx(dest, &size)) {
      err = errhr(HRESULT_FROM_WIN32(GetLastError()));
      goto cleanup;
    }
    static char const *suffixes[] = {
        "byte(s)",
        "KB",
        "MB",
        "GB",
        "TB",
        "PB",
        "EB",
    };
    size_t const suffix = bytes_to_human_readable(b, (uint64_t)size.QuadPart, '.');
    mo_snprintf_wchar(msg, sizeof(msg) / sizeof(msg[0]), L"%1$hs%2$hs", "File size: %1$hs %2$hs", b, suffixes[suffix]);
    params->on_log_line(params->userdata, msg);
  }

  if (info) {
    *info = (struct raw2opus_info){
        .sample_rate = fi.audio_rate,
        .channels = fi.audio_ch,
        .samples = ctx.samples,
    };
  }
cleanup:
  if (enc) {
    ope_encoder_destroy(enc);
    enc = NULL;
  }
  if (comments) {
    ope_comments_destroy(comments);
    comments = NULL;
  }
  if (dest != INVALID_HANDLE_VALUE) {
    CloseHandle(dest);
    dest = INVALID_HANDLE_VALUE;
    if (efailed(err)) {
      DeleteFileW(params->opus_path);
    }
  }
  return err;
}
