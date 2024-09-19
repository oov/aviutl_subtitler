#pragma once

#include <ovbase.h>

#include "aviutl.h"
#include "config.h"

struct processor;

enum processor_type {
  processor_type_invalid = 0,
  processor_type_raw2opus = 1,
  processor_type_opus2json = 2,
  processor_type_json2exo = 3,
};

struct processor_exo_info {
  wchar_t const *exo_path;
  int length;
  int layer_min;
  int layer_max;
};

struct processor_params {
  HINSTANCE hinst;
  FILTER *fp;
  void *editp;

  void *userdata;
  void (*on_start)(void *const userdata, enum processor_type const type);
  void (*on_progress)(void *const userdata, enum processor_type const type, int const progress);
  void (*on_log_line)(void *const userdata, enum processor_type const type, wchar_t const *const message);
  void (*on_create_exo)(void *const userdata, struct processor_exo_info const *const info);
  void (*on_finish)(void *const userdata, enum processor_type const type, error err);
  void (*on_complete)(void *const userdata, bool const success);
};

NODISCARD error processor_create(struct processor **const pp, struct processor_params const *const params);
void processor_destroy(struct processor **const pp);
struct config *processor_get_config(struct processor *const p);

NODISCARD error processor_run(struct processor *const p);
NODISCARD error processor_run_solo(struct processor *const p, enum processor_type const type);
NODISCARD error processor_abort(struct processor *const p);

/**
 * @struct processor_module
 * @brief Represents a Lua module with its details.
 *
 * This structure holds information about a Lua module, including its identifier, name, and description.
 */
struct processor_module {
  wchar_t const *module;      /**< The name used to require the module in Lua. */
  wchar_t const *name;        /**< The human-readable name of the module. */
  wchar_t const *description; /**< A brief description of the module. */
};

/**
 * @brief Retrieves the list of available Lua modules.
 *
 * processor_get_modules populates the provided pointer with a list of available Lua modules.
 * The length of the list can be obtained using the OV_ARRAY_LENGTH macro.
 *
 * This function internally executes Lua scripts to load the modules,
 * and the on_log_line callback may be invoked during this process.
 * The callback function is called on the same thread as the caller.
 *
 * @param p A pointer to the processor.
 * @param pmpp A pointer to a pointer that will be set to the list of Lua modules.
 * @return An error code indicating success or failure.
 */
NODISCARD error processor_get_modules(struct processor *const p, struct processor_module **const pmpp);

/**
 * @brief Destroys the list of Lua modules.
 *
 * processor_module_destroy frees the memory allocated for the list of Lua modules.
 *
 * @param pmpp A pointer to a pointer to the list of Lua modules to be destroyed.
 */
void processor_module_destroy(struct processor_module **const pmpp);
