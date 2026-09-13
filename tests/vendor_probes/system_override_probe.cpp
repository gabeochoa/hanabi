// afterhours_gaps.md #593: System<>'s six overrides carry no `override`.
// Compiled with the library as USER code (-I, not -isystem) and
// -Werror=inconsistent-missing-override: fails at the pin, compiles after
// vendor_patches/593-system-override.patch. Reached through the tree's
// path, so the probe is about the header and not about the include policy.
#define FMT_HEADER_ONLY
#include "afterhours/src/core/system.h"

struct Probe : afterhours::System<> {};

int main() { return 0; }
