#include "export_audio.h"

#include <stdatomic.h>

#include <ovthreads.h>

#include "aviutl.h"
#include "i18n.h"

struct export_audio {
  atomic_bool abort_requested;
  thrd_t th;
  error err;

  struct export_audio_options options;
};

static int worker(void *userdata) {
  error err = eok();
  struct export_audio *ea = userdata;
  FILTER *const fp = ea->options.fp;
  void *const editp = ea->options.editp;
  void *buffer = NULL;
  int s, e;
  if (!fp->exfunc->get_select_frame(editp, &s, &e)) {
    s = 0;
    e = fp->exfunc->get_frame_n(editp) - 1;
  }

  FILE_INFO fi = {0};
  if (!fp->exfunc->get_file_info(editp, &fi)) {
    err = errg(err_fail);
    goto cleanup;
  }

  // It seems that AviUtl sometimes writes to a buffer of two or more frames instead of one.
  // If the buffer size is reserved just below the required buffer size, it will result in buffer overrun.
  // To avoid this problem, reserve a larger buffer size.
  size_t const samples_per_frame = (size_t)((fi.audio_rate * fi.video_scale * 5) / (fi.video_rate * 2)) + 32;
  err = mem_aligned_alloc(&buffer, samples_per_frame * (size_t)(fi.audio_ch), sizeof(int16_t), 16);
  if (efailed(err)) {
    err = ethru(err);
    goto cleanup;
  }

  for (int i = s; i <= e; ++i) {
    if (atomic_load(&ea->abort_requested)) {
      err = errg(err_abort);
      goto cleanup;
    }
    int const samples = fp->exfunc->get_audio_filtered(editp, i, buffer);
    if (efailed(ea->err)) {
      err = ethru(ea->err);
      ea->err = eok();
      goto cleanup;
    }
    ea->options.on_read(ea->options.userdata, buffer, (size_t)(samples), ((i - s + 1) * 10000) / (e - s + 1));
  }
cleanup:
  if (buffer) {
    ereport(mem_aligned_free(&buffer));
  }
  ea->options.on_finish(ea->options.userdata, err);
  return 0;
}

NODISCARD error export_audio_create(struct export_audio **const eap, struct export_audio_options const *const options) {
  if (!eap || *eap || !options || !options->editp || !options->fp || !options->on_read || !options->on_finish) {
    return errg(err_invalid_arugment);
  }
  error err = eok();
  struct export_audio *ea = NULL;

  err = mem(&ea, 1, sizeof(struct export_audio));
  if (efailed(err)) {
    goto cleanup;
  }
  *ea = (struct export_audio){
      .err = eok(),
      .options = *options,
  };
  atomic_init(&ea->abort_requested, false);
  if (thrd_create(&ea->th, worker, ea) != thrd_success) {
    err = errg(err_fail);
    goto cleanup;
  }
  *eap = ea;
  ea = NULL;
cleanup:
  if (ea) {
    ereport(mem_free(&ea));
  }
  return err;
}

void export_audio_destroy(struct export_audio **const eap) {
  if (!eap || !*eap) {
    return;
  }
  struct export_audio *ea = *eap;
  thrd_join(ea->th, NULL);
  if (efailed(ea->err)) {
    ereport(ea->err);
  }
  ereport(mem_free(eap));
}

void export_audio_abort(struct export_audio *const ea) {
  if (!ea) {
    return;
  }
  atomic_store(&ea->abort_requested, true);
}
