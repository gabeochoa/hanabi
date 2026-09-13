// The one afterhours translation unit hanabi compiles, reached the way every
// other afterhours header is reached: through <afterhours/...>, i.e. the
// -isystem vendor/ include path. Compiled by its own path the file's relative
// includes are user headers and the library's warnings (system.h's missing
// `override`s, afterhours_gaps.md #593) surface as hanabi's; through the
// system path they fall under the same third-party policy as every header.
#include <afterhours/src/plugins/files.cpp>
