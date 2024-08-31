#include "jsoncommon.h"

#include <ovbase.h>

static void *json_malloc(void *ctx, size_t size) {
  (void)ctx;
  void *ptr = NULL;
  error err = mem(&ptr, size, sizeof(char));
  if (efailed(err)) {
    ereport(err);
    return NULL;
  }
  return ptr;
}

static void *json_realloc(void *ctx, void *ptr, size_t old_size, size_t size) {
  (void)ctx;
  (void)old_size;
  error err = mem(&ptr, size, sizeof(char));
  if (efailed(err)) {
    ereport(err);
    return NULL;
  }
  return ptr;
}

static void json_free(void *ctx, void *ptr) {
  (void)ctx;
  ereport(mem_free(&ptr));
}

struct yyjson_alc const *jsoncommon_get_json_alc(void) {
  static struct yyjson_alc const alc = {
      .malloc = json_malloc,
      .realloc = json_realloc,
      .free = json_free,
      .ctx = NULL,
  };
  return &alc;
}
