#pragma once

#ifdef __GNUC__
#  ifndef __has_warning
#    define __has_warning(x) 0
#  endif
#  pragma GCC diagnostic push
#  if __has_warning("-Wdocumentation")
#    pragma GCC diagnostic ignored "-Wdocumentation"
#  endif
#endif // __GNUC__
#include <yyjson.h>
#ifdef __GNUC__
#  pragma GCC diagnostic pop
#endif // __GNUC__

struct yyjson_alc const *jsoncommon_get_json_alc(void);
