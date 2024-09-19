#pragma once

#include <ovbase.h>

/**
 * @brief Parameters for the opus2json conversion function.
 */
struct opus2json_params {
  wchar_t const *opus_path;       /**< Path to the input *.opus file. */
  wchar_t const *whisper_path;    /**< Path to the Whisper model file. */
  wchar_t const *additional_args; /**< Additional arguments for the conversion process. */
  void *userdata;                 /**< User-defined data passed to the callbacks. */
  /**
   * @brief Callback function to report progress.
   * @param userdata User-defined data passed to the callback.
   * @param progress Progress value ranging from 0 to 10000.
   * @return Returns false to abort the conversion process.
   * @note This function is called periodically even if there is no progress, to provide an opportunity to abort.
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
 * @brief Generates a *.json file from an *.opus file using Whisper.
 * This function blocks execution until the generation is complete.
 * The on_progress and on_log_line callbacks are invoked from the same thread as the caller.
 * If the on_progress callback returns false, the generation process is aborted, and the function returns
 * errg(err_abort).
 * @param params Pointer to the parameters required for the generation.
 * @return An error object.
 */
NODISCARD error opus2json(struct opus2json_params const *const params);
