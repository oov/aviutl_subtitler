#include "raw2opus.h"

#include <stdatomic.h>

#include <ovarray.h>
#include <ovprintf.h>
#include <ovthreads.h>
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

struct raw2opus_context {
  struct raw2opus_params params;
  struct raw2opus_info info;
  OggOpusEnc *enc;
  OggOpusComments *comments;
  mtx_t mtx;
  HANDLE dest;
  struct export_audio *ea;
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
  if (efailed(ctx->err)) {
    export_audio_abort(ctx->ea);
  }
  return efailed(ctx->err) ? -1 : 0;
}

static int opus_close(void *userdata) {
  struct raw2opus_context *const ctx = userdata;
  error err = eok();
  if (ctx->err) {
    goto cleanup;
  }
  if (!CloseHandle(ctx->dest)) {
    err = errhr(HRESULT_FROM_WIN32(GetLastError()));
    goto cleanup;
  }
  ctx->dest = INVALID_HANDLE_VALUE;
cleanup:
  if (efailed(err)) {
    if (efailed(ctx->err)) {
      efree(&err);
    } else {
      ctx->err = err;
      err = eok();
    }
  }
  return efailed(ctx->err) ? -1 : 0;
}

static void export_audio_read(void *const userdata, void *const p, size_t const samples, int const progress) {
  struct raw2opus_context *const ctx = userdata;
  error err = eok();
  mtx_lock(&ctx->mtx);
  if (ctx->err) {
    goto cleanup;
  }
  int const r = ope_encoder_write(ctx->enc, p, (int)samples);
  if (r < 0) {
    err = emsg_i18nf(err_type_generic, err_fail, L"%1$hs", gettext("Unable to write file: %1$hs"), ope_strerror(r));
    goto cleanup;
  }
  ctx->info.samples += samples;
  if (ctx->params.on_progress) {
    ctx->params.on_progress(ctx->params.userdata, progress);
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
  if (efailed(ctx->err)) {
    export_audio_abort(ctx->ea);
  }
  mtx_unlock(&ctx->mtx);
}

static void export_audio_finish(void *const userdata, error err) {
  struct raw2opus_context *ctx = userdata;
  if (efailed(err)) {
    goto cleanup;
  }
  mtx_lock(&ctx->mtx);
  int r = ope_encoder_drain(ctx->enc);
  if (r < 0) {
    err = emsg_i18nf(err_type_generic, err_fail, L"%1$hs", gettext("Unable to drain: %1$hs"), ope_strerror(r));
    goto cleanup;
  }
cleanup:
  if (ctx->dest != INVALID_HANDLE_VALUE) {
    CloseHandle(ctx->dest);
    ctx->dest = INVALID_HANDLE_VALUE;
    if (efailed(err)) {
      DeleteFileW(ctx->params.opus_path);
    }
  }
  if (efailed(err)) {
    if (efailed(ctx->err)) {
      efree(&err);
    } else {
      ctx->err = err;
      err = eok();
    }
  }
  void (*on_finish)(void *const userdata, struct raw2opus_info const *const info, error err) = ctx->params.on_finish;
  void *ud = ctx->params.userdata;
  struct raw2opus_info const info = ctx->info;
  error e = ctx->err;
  mtx_unlock(&ctx->mtx);
  if (on_finish) {
    on_finish(ud, &info, e);
  }
}

NODISCARD error raw2opus_create(struct raw2opus_context **const ctxpp, struct raw2opus_params const *const params) {
  if (!ctxpp || *ctxpp || !params || !params->fp || !params->editp || !params->opus_path) {
    return errg(err_invalid_arugment);
  }

  struct raw2opus_context *ctx = NULL;
  FILTER *fp = params->fp;
  FILE_INFO fi = {0};
  HANDLE dest = INVALID_HANDLE_VALUE;
  OggOpusEnc *enc = NULL;
  OggOpusComments *comments = NULL;
  struct export_audio *ea = NULL;

  error err = mem(&ctx, 1, sizeof(struct raw2opus_context));
  if (efailed(err)) {
    err = ethru(err);
    goto cleanup;
  }
  *ctx = (struct raw2opus_context){
      .err = eok(),
      .params = *params,
  };
  mtx_init(&ctx->mtx, mtx_plain);
  mtx_lock(&ctx->mtx);
  if (!fp->exfunc->get_file_info(params->editp, &fi)) {
    err = emsg_i18n(err_type_generic, err_fail, gettext("Unable to retrieve file information."));
    goto cleanup;
  }

  dest = CreateFileW(params->opus_path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
  if (dest == INVALID_HANDLE_VALUE) {
    err = errhr(HRESULT_FROM_WIN32(GetLastError()));
    goto cleanup;
  }

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
      ctx,
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

  err = export_audio_create(&ea,
                            &(struct export_audio_options){
                                .editp = params->editp,
                                .fp = params->fp,
                                .userdata = ctx,
                                .on_read = export_audio_read,
                                .on_finish = export_audio_finish,
                            });
  if (efailed(err)) {
    err = ethru(err);
    goto cleanup;
  }
  ctx->info = (struct raw2opus_info){
      .sample_rate = fi.audio_rate,
      .channels = fi.audio_ch,
      .samples = 0,
  };
  ctx->enc = enc;
  ctx->comments = comments;
  ctx->dest = dest;
  ctx->ea = ea;
  dest = INVALID_HANDLE_VALUE;
  ea = NULL;
  enc = NULL;
  comments = NULL;
  if (ctx->params.on_log_line) {
    wchar_t buf[1024];
    mo_snprintf_wchar(buf, sizeof(buf) / sizeof(buf[0]), L"%1$hs", gettext("Encoding audio to Opus..."));
    ctx->params.on_log_line(ctx->params.userdata, buf);
    mo_snprintf_wchar(buf, sizeof(buf) / sizeof(buf[0]), L"%1$ls", "%1$ls", params->opus_path);
    ctx->params.on_log_line(ctx->params.userdata, buf);
  }
  mtx_unlock(&ctx->mtx);
  *ctxpp = ctx;
  ctx = NULL;
cleanup:
  if (ctx) {
    mtx_unlock(&ctx->mtx);
    mtx_destroy(&ctx->mtx);
    ereport(mem_free(&ctx));
  }
  if (ea) {
    export_audio_abort(ea);
    export_audio_destroy(&ea);
  }
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
    DeleteFileW(params->opus_path);
  }
  return err;
}

void raw2opus_destroy(struct raw2opus_context **const ctxpp) {
  if (!ctxpp || !*ctxpp) {
    return;
  }
  struct raw2opus_context *ctx = *ctxpp;
  if (ctx->enc) {
    ope_encoder_destroy(ctx->enc);
    ctx->enc = NULL;
  }
  if (ctx->comments) {
    ope_comments_destroy(ctx->comments);
    ctx->comments = NULL;
  }
  if (ctx->ea) {
    export_audio_destroy(&ctx->ea);
  }
  mtx_destroy(&ctx->mtx);
  ereport(mem_free(ctxpp));
}

NODISCARD error raw2opus_abort(struct raw2opus_context *const ctx) {
  if (ctx->ea) {
    export_audio_abort(ctx->ea);
  }
  return eok();
}
