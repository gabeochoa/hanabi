#include <branding.h>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <set>
#include <string>
#include <vector>

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
    const std::string url = std::string(product_branding::kUrlScheme) +
                            "://thread/space%2Fid%20one?source=spotlight";
    native_simulate_open_url(url.c_str());
    CHECK(native_take_open_thread(thread, sizeof(thread)));
    CHECK(std::strcmp(thread, "space/id one") == 0);
    native_simulate_open_url("https://example.invalid/thread/nope");
    CHECK(!native_take_open_thread(thread, sizeof(thread)));

    char dropped[256] = {};
    CHECK(!native_dropped_image_pending());
    native_simulate_file_drop("/tmp/idle-wake.png");
    CHECK(native_dropped_image_pending());
    CHECK(native_take_dropped_image(dropped, sizeof(dropped)));
    CHECK(std::strcmp(dropped, "/tmp/idle-wake.png") == 0);
    CHECK(!native_dropped_image_pending());

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

    const int face_count = native_font_faces(nullptr, 0);
    CHECK(face_count > 0);
    std::vector<NativeFontFace> faces(static_cast<std::size_t>(face_count));
    CHECK(native_font_faces(faces.data(), face_count) == face_count);
    std::set<std::string> keys;
    bool system_regular = false;
    bool system_bold = false;
    for (const auto& face : faces) {
        CHECK(face.family[0] != '\0');
        CHECK(face.weight[0] != '\0');
        CHECK(face.path[0] == '/');
        CHECK(face.point_scale > 1.0f && face.point_scale < 2.0f);
        CHECK(std::filesystem::is_regular_file(face.path));
        CHECK(keys.insert(std::string(face.family) + "/" + face.weight).second);
        if (std::strcmp(face.family, "system") == 0 &&
            std::strcmp(face.weight, "regular") == 0)
            system_regular = true;
        if (std::strcmp(face.family, "system") == 0 &&
            std::strcmp(face.weight, "bold") == 0)
            system_bold = true;
    }
    CHECK(system_regular);
    CHECK(system_bold);

    if (failures == 0) {
        std::printf("OK\n");
        return 0;
    }
    std::printf("%d failure(s)\n", failures);
    return 1;
}
