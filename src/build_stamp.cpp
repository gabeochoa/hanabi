#include "build_stamp.h"

#ifndef HANABI_BUILD_STAMP_VALUE
#define HANABI_BUILD_STAMP_VALUE "unknown"
#endif

namespace hanabi {
const char* build_stamp() { return HANABI_BUILD_STAMP_VALUE; }
}
