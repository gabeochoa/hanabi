#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <set>
#include <sstream>
#include <string>

#include <nlohmann/json.hpp>

#include "../../src/api/mock_faults.h"
#include "../../src/api/mock_journal.h"

namespace mj = api::mock_journal;
namespace mf = api::mock_faults;

static int failures = 0;
#define CHECK(cond)                                                    \
    do {                                                               \
        if (!(cond)) {                                                 \
            std::printf("  FAIL: %s (line %d)\n", #cond, __LINE__);    \
            ++failures;                                                \
        }                                                              \
    } while (0)

static std::string slurp(const std::string& p) {
    std::ifstream in(p);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

int main() {
    std::printf("=== test_mock_knobs ===\n");
    const std::string path = std::string(std::getenv("TMPDIR") ? std::getenv("TMPDIR") : "/tmp") +
                             "/hanabi-test-mock-journal-" + std::to_string(static_cast<long>(::getpid())) + ".jsonl";
    CHECK(mj::clear(path));
    CHECK(slurp(path).empty());
    mj::Entry a{"t2", "hand", "hello **there**", 1, 1700000000000LL, "local-1"};
    mj::Entry b{"t6", "journal", "second", 0, 1700000001000LL, ""};
    CHECK(mj::append(a, path) && mj::append(b, path));
    const std::string all = slurp(path);
    CHECK(std::count(all.begin(), all.end(), '\n') == 2);
    const auto first = nlohmann::json::parse(all.substr(0, all.find('\n')));
    CHECK(first["session_id"] == "t2" && first["kind"] == "hand" && first["text"] == "hello **there**" &&
          first["attachments"] == 1 && first["at_unix_ms"] == 1700000000000LL && first["local_id"] == "local-1");
    const auto second = nlohmann::json::parse(all.substr(all.find('\n') + 1));
    CHECK(second["kind"] == "journal" && !second.contains("local_id"));
    CHECK(mj::clear(path) && slurp(path).empty());
    CHECK(!mj::append(a, ""));
    CHECK(!mj::clear(""));
    std::remove(path.c_str());

    CHECK(mf::open_fail_once_session("t2:offline") == "t2");
    CHECK(mf::open_fail_once_session("t2") == "t2");
    CHECK(mf::open_fail_once_why("t2:the socket closed") == "the socket closed");
    CHECK(mf::open_fail_once_why("t2") == "the backend did not answer the open");
    std::set<std::string> consumed;
    std::string why;
    CHECK(mf::consume_open_failure("t2", "t2:offline", consumed, &why) && why == "offline");
    CHECK(!mf::consume_open_failure("t2", "t2:offline", consumed, &why));
    CHECK(!mf::consume_open_failure("t6", "t2:offline", consumed, &why));
    CHECK(!mf::consume_open_failure("t2", "", consumed, &why));
    CHECK(mf::consume_open_failure("t2", "t2:again", consumed, nullptr));

    if (failures == 0) {
        std::printf("OK\n");
        return 0;
    }
    std::printf("%d failure(s)\n", failures);
    return 1;
}
