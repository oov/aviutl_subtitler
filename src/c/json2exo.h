#pragma once

#include <ovbase.h>

#include "aviutl.h"

/**
 * @brief Information about the generated *.exo file.
 */
struct json2exo_info {
  int frames;      /**< Total frames of the *.exo file. */
  int layer_min;   /**< Minimum layer index. */
  int layer_max;   /**< Maximum layer index. */
  int num_objects; /**< Number of objects in the *.exo file. */
};

/**
 * @brief Parameters for the json2exo conversion function.
 */
struct json2exo_params {
  FILTER *fp;
  void *editp;
  wchar_t const *json_path;     /**< Path to the input *.json file. */
  wchar_t const *exo_path;      /**< Path to the output *.exo file. */
  wchar_t const *lua_directory; /**< Directory containing Lua scripts. */
  wchar_t const *module;        /**< Lua module name used for the conversion process. */
  void *userdata;               /**< User-defined data passed to the callbacks. */
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
 * @brief Converts a *.json file to an *.exo file.
 * This function blocks execution until the conversion is complete.
 * The on_progress and on_log_line callbacks are invoked from the same thread as the caller.
 * @note If the on_progress callback returns false, the conversion process is aborted, and the function returns
 * errg(err_abort).
 * @param params Pointer to the parameters required for the conversion.
 * @param info Pointer to the structure that will store information about the generated *.exo file.
 * @return An error object indicating the success or failure of the conversion process.
 */
NODISCARD error json2exo(struct json2exo_params const *const params, struct json2exo_info *const info);
