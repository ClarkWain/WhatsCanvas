#pragma once

#if defined(_WIN32) || defined(__CYGWIN__)
#  if defined(WSC_SHARED)
#    if defined(WSC_EXPORTS)
#      define WSC_API __declspec(dllexport)
#    else
#      define WSC_API __declspec(dllimport)
#    endif
#  else
#    define WSC_API
#  endif
#elif defined(__GNUC__) && __GNUC__ >= 4
#  define WSC_API __attribute__((visibility("default")))
#else
#  define WSC_API
#endif