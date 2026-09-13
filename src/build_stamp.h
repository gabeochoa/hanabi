#pragma once

// The short hash of the commit the binary was built from, for the window
// title ("am I even on the new build?"). Defined in build_stamp.cpp, the one
// translation unit whose compile takes the hash (-DHANABI_BUILD_STAMP_VALUE),
// so a new commit rebuilds one three-line file and relinks -- not main.cpp,
// and not the tree.
namespace hanabi {
const char* build_stamp();
}
