#pragma once

#include <ovbase.h>

/**
 * @brief Information about the raw2opus conversion.
 */
struct raw2opus_info {
  int sample_rate; /**< Sample rate of the audio. */
  int channels;    /**< Number of audio channels. */
  size_t samples;  /**< Number of audio samples. */
};

/**
 * @brief Parameters for the raw2opus conversion function.
 */
struct raw2opus_params {
  void *fp;                 /**< File pointer for the input raw audio data. */
  void *editp;              /**< Edit pointer for the timeline data. */
  wchar_t const *opus_path; /**< Path to the output *.opus file. */
  void *userdata;           /**< User-defined data passed to the callbacks. */
  /**
   * @brief Callback function to report progress.
   * @param userdata User-defined data passed to the callback.
   * @param progress Progress value ranging from 0 to 10000.
   * @return Returns false to abort the conversion process.
   */
  bool (*on_progress)(void *const userdata, int const progress);
  /**
   * @brief Callback function to log messages.
   * @param userdata User-defined data passed to the callback.
   * @param message Log message.
   */
  void (*on_log_line)(void *const userdata, wchar_t const *const message);
};

/**
 * @brief Encodes raw audio data from a timeline to an *.opus file.
 * This function blocks execution until the encoding is complete.
 * The on_progress and on_log_line callbacks are invoked from the same thread as the caller.
 * If the on_progress callback returns false, the encoding process is aborted, and the function returns errg(err_abort).
 * @param params Pointer to the parameters required for the encoding.
 * @param info Pointer to the structure that will store information about the generated *.opus file.
 * @return An error object indicating the success or failure of the encoding process.
 */
NODISCARD error raw2opus(struct raw2opus_params const *const params, struct raw2opus_info *const info);
