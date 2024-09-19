#include "export_audio.h"

#include "aviutl.h"
#include "i18n.h"

NODISCARD error export_audio(struct export_audio_params const *const params) {
  if (!params || !params->editp || !params->fp || !params->on_read) {
    return errg(err_invalid_arugment);
  }
  error err = eok();

  FILTER *const fp = params->fp;
  void *const editp = params->editp;
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
    int const samples = fp->exfunc->get_audio_filtered(editp, i, buffer);
    if (!params->on_read(params->userdata, buffer, (size_t)(samples), ((i - s + 1) * 10000) / (e - s + 1))) {
      err = errg(err_abort);
      goto cleanup;
    }
  }
cleanup:
  if (buffer) {
    ereport(mem_aligned_free(&buffer));
  }
  return err;
}
