#include <cstdio>
#include <fstream>
#include <string>
#include <thread>
#include <chrono>
#include <filesystem>

#include "../../src/util/update_ready.h"

static int failures = 0;
#define CHECK(cond)                                               \
    do {                                                          \
        if (!(cond)) {                                            \
            std::printf("FAIL: %s (line %d)\n", #cond, __LINE__); \
            ++failures;                                           \
        }                                                         \
    } while (0)

namespace ur = hanabi::update_ready;

int main() {
    const std::string dir = "/tmp/hanabi_test_update_ready";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    const std::string exe = dir + "/hanabi.exe";
    { std::ofstream(exe) << "build one"; }
    ur::Watch w;
    w.path = exe;
    w.at_launch = ur::identity_of(exe);
    CHECK(w.at_launch.valid());
    CHECK(!w.pending(ur::identity_of(exe)));  // nothing changed

    // An install replaces the file (new inode): pending.
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    { std::ofstream(dir + "/next") << "build two, longer"; }
    std::filesystem::rename(dir + "/next", exe);
    const ur::FileId two = ur::identity_of(exe);
    CHECK(w.pending(two));

    // Later dismisses that build only.
    w.dismissed = two;
    CHECK(!w.pending(two));
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    { std::ofstream(dir + "/next") << "build three"; }
    std::filesystem::rename(dir + "/next", exe);
    CHECK(w.pending(ur::identity_of(exe)));  // a newer one shows again

    // A file missing mid-install says nothing.
    std::filesystem::remove(exe);
    CHECK(!w.pending(ur::identity_of(exe)));

    CHECK(ur::bundle_of("/Applications/Hanabi.app/Contents/MacOS/hanabi") == "/Applications/Hanabi.app");
    CHECK(ur::bundle_of("/Users/x/hanabi/output/hanabi.exe").empty());
    CHECK(!ur::running_executable().empty());
    std::filesystem::remove_all(dir);
    if (failures == 0) {
        std::printf("OK\n");
        return 0;
    }
    return 1;
}
