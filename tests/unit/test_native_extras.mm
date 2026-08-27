#include <cstdio>
#include <cstring>

#include "../../src/native_extras.h"

static int failures = 0;
#define CHECK(value)                                                        \
    do {                                                                    \
        if (!(value)) {                                                     \
            std::printf("FAIL: %s line %d\n", #value, __LINE__);           \
            ++failures;                                                     \
        }                                                                   \
    } while (0)

int main() {
    char thread[128] = {};
    CHECK(!native_take_open_thread(thread, sizeof(thread)));
    native_simulate_notification_click("thread/from-notification");
    CHECK(native_take_open_thread(thread, sizeof(thread)));
    CHECK(std::strcmp(thread, "thread/from-notification") == 0);
    CHECK(!native_take_open_thread(thread, sizeof(thread)));
    native_simulate_open_url("hanabi://thread/space%2Fid%20one?source=spotlight");
    CHECK(native_take_open_thread(thread, sizeof(thread)));
    CHECK(std::strcmp(thread, "space/id one") == 0);
    native_simulate_open_url("https://example.invalid/thread/nope");
    CHECK(!native_take_open_thread(thread, sizeof(thread)));

    char status[512] = {};
    native_integration_status(status, sizeof(status));
    CHECK(std::strstr(status, "bundle=") != nullptr);
    CHECK(std::strstr(status, "notifications=") != nullptr);
    CHECK(std::strstr(status, "spotlight=") != nullptr);

    native_notify("headless", "must not post", "thread", true);
    native_spotlight_sync(nullptr, 0);
    native_integration_status(status, sizeof(status));
    CHECK(std::strstr(status, "notifications=non-bundled") != nullptr);
    CHECK(std::strstr(status, "spotlight=non-bundled") != nullptr);

    if (failures == 0) {
        std::printf("OK\n");
        return 0;
    }
    std::printf("%d failure(s)\n", failures);
    return 1;
}
