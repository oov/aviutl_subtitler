#pragma once

#include <ovbase.h>

/**
 * @brief Parameters for the export_audio function.
 */
struct export_audio_params {
  void *editp;    /**< Edit pointer for the timeline data. */
  void *fp;       /**< File pointer for the output audio data. */
  void *userdata; /**< User-defined data passed to the callbacks. */
  /**
   * @brief Callback function called each time audio data is read.
   * @param userdata User-defined data passed to the callback.
   * @param p Pointer to the interleaved 16-bit integer audio data.
   * @param samples Number of audio samples read.
   * @param progress Progress value ranging from 0 to 10000.
   * @return Returns false to abort the export process.
   * @note The length of the data is samples * channels * sizeof(int16_t).
   */
  bool (*on_read)(void *const userdata, void *const p, size_t const samples, int const progress);
};

/**
 * @brief Exports audio data from the timeline.
 * This function blocks execution until the export is complete.
 * The on_read callback is invoked each time audio data is read.
 * If the on_read callback returns false, the export process is aborted, and the function returns errg(err_abort).
 * @param params Pointer to the parameters required for the export.
 * @return An error object indicating the success or failure of the export process.
 */
NODISCARD error export_audio(struct export_audio_params const *const params);
