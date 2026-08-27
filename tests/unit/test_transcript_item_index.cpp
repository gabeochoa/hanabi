#include <algorithm>
#include <cmath>
#include <cstdio>
#include <random>
#include <set>
#include <string>
#include <vector>

#include "../../src/ecs/transcript_item_index.h"

using ecs::model::TranscriptGeometryFacts;
using ecs::model::TranscriptItem;
using ecs::model::TranscriptItemIndex;
using ecs::model::TranscriptMutation;
using ecs::model::TranscriptMutationKind;

static int failures = 0;
#define CHECK(x) do { if (!(x)) { std::printf("FAIL line %d: %s\n", __LINE__, #x); ++failures; } } while (0)

struct Message {
    int id = 0;
    int kind = 0;
    int text = 1;
    int day = 0;
};

struct Model {
    std::vector<Message> messages;
    std::set<int> expanded;
    TranscriptGeometryFacts facts;
    TranscriptMutation mutation;
};

static void bump(Model& model, TranscriptMutationKind kind, std::size_t first,
                 std::size_t count) {
    const std::uint64_t old = model.mutation.revision;
    model.mutation = TranscriptMutation{old, old + 1, kind, first, count};
}

static float message_height(const Message& message, const Model& model) {
    float height = 10.0f + static_cast<float>(message.text % 17);
    height += model.facts.pane_width * 0.01f;
    if (model.expanded.count(message.id) != 0) height += 31.0f;
    if (model.facts.streaming &&
        model.facts.live_index < model.messages.size() &&
        model.messages[model.facts.live_index].id == message.id)
        height += static_cast<float>(model.facts.stream_phase + 1);
    return height;
}

static void build_from(const Model& model, std::size_t start,
                       std::vector<TranscriptItem>& out) {
    std::size_t i = start;
    while (i < model.messages.size()) {
        if (model.facts.show_date_dividers && i > 0 &&
            model.messages[i - 1].day != model.messages[i].day) {
            TranscriptItem divider;
            divider.kind = TranscriptItem::DateDivider;
            divider.lo = static_cast<int>(i);
            divider.height = 7.0f;
            out.push_back(divider);
        }
        if (static_cast<int>(i) == model.facts.unread_first) {
            TranscriptItem divider;
            divider.kind = TranscriptItem::NewDivider;
            divider.lo = static_cast<int>(i);
            divider.hi = model.facts.unread_count;
            divider.height = 9.0f;
            out.push_back(divider);
        }
        if (model.messages[i].kind == 1) {
            std::size_t end = i + 1;
            while (end < model.messages.size() && model.messages[end].kind == 1)
                ++end;
            TranscriptItem item;
            item.kind = end - i > 1 ? TranscriptItem::ToolPile
                                    : TranscriptItem::ToolBlock;
            item.lo = static_cast<int>(i);
            item.hi = static_cast<int>(end);
            item.height = 13.0f * static_cast<float>(end - i);
            out.push_back(item);
            i = end;
            continue;
        }
        TranscriptItem item;
        item.kind = TranscriptItem::Bubble;
        item.lo = static_cast<int>(i);
        item.height = message_height(model.messages[i], model);
        item.showAuthor = i == 0 || model.messages[i - 1].kind != 0;
        out.push_back(item);
        ++i;
    }
}

static std::vector<TranscriptItem> reference(const Model& model) {
    std::vector<TranscriptItem> out;
    build_from(model, 0, out);
    return out;
}

static bool same(const std::vector<TranscriptItem>& a,
                 const std::vector<TranscriptItem>& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (a[i].kind != b[i].kind || a[i].lo != b[i].lo ||
            a[i].hi != b[i].hi || a[i].isLive != b[i].isLive ||
            a[i].showAuthor != b[i].showAuthor ||
            std::fabs(a[i].height - b[i].height) > 0.001f)
            return false;
    }
    return true;
}

static TranscriptItemIndex::View update(TranscriptItemIndex& index,
                                        const std::string& key, Model& model) {
    return index.update(key, model.messages.data(), model.messages.size(),
                        model.mutation, model.facts,
                        [&](std::size_t start, std::vector<TranscriptItem>& out) {
                            build_from(model, start, out);
                        });
}

static void check_reference(TranscriptItemIndex& index, const std::string& key,
                            Model& model) {
    const auto view = update(index, key, model);
    CHECK(view.items != nullptr);
    CHECK(same(*view.items, reference(model)));
    float total = 0.0f;
    for (const auto& item : *view.items) total += item.height;
    CHECK(std::fabs(total - view.height) < 0.001f);
}

static Model seeded(int count) {
    Model model;
    model.facts.pane_width = 500.0f;
    model.facts.show_date_dividers = true;
    model.facts.unread_first = count / 2;
    model.facts.unread_count = count - model.facts.unread_first;
    for (int i = 0; i < count; ++i)
        model.messages.push_back(Message{i + 1, i % 5 == 2 ? 1 : 0,
                                         3 + i * 7, i / 9});
    return model;
}

static void test_unchanged_is_constant_work() {
    TranscriptItemIndex index;
    Model model = seeded(4000);
    const auto cold = update(index, "0/thread", model);
    CHECK(cold.messages_visited == model.messages.size());
    for (int frame = 0; frame < 100; ++frame) {
        const auto hit = update(index, "0/thread", model);
        CHECK(!hit.rebuilt);
        CHECK(hit.messages_visited == 0);
    }
}

static void test_bounded_and_pane_width_isolated() {
    TranscriptItemIndex index;
    Model left = seeded(40);
    Model right = left;
    right.facts.pane_width = 320.0f;
    check_reference(index, "0/same", left);
    check_reference(index, "1/same", right);
    CHECK(index.slots() == 2);
    CHECK(update(index, "0/same", left).messages_visited == 0);
    CHECK(update(index, "1/same", right).messages_visited == 0);
    for (int i = 0; i < 20; ++i)
        check_reference(index, std::to_string(i) + "/thread", left);
    CHECK(index.slots() == TranscriptItemIndex::kMaxSlots);
}

static void test_randomized_differential() {
    std::mt19937 rng(0x51A7E);
    TranscriptItemIndex index;
    Model model = seeded(80);
    check_reference(index, "0/random", model);
    int next_id = 1000;

    for (int step = 0; step < 3000; ++step) {
        const int op = static_cast<int>(rng() % 8);
        if (op == 0) {
            const std::size_t first = model.messages.size();
            model.messages.push_back(Message{next_id++, static_cast<int>(rng() % 3 == 0),
                                             static_cast<int>(rng() % 100),
                                             model.messages.empty() ? 0
                                                 : model.messages.back().day});
            bump(model, TranscriptMutationKind::Append, first, 1);
        } else if (op == 1 && !model.messages.empty()) {
            const std::size_t at = model.messages.size() - 1;
            model.messages[at].text += 1 + static_cast<int>(rng() % 11);
            bump(model, TranscriptMutationKind::Update, at, 1);
        } else if (op == 2 && model.messages.size() > 2) {
            const std::size_t at = 1 + rng() % (model.messages.size() - 1);
            model.messages[at].kind = 1 - model.messages[at].kind;
            bump(model, TranscriptMutationKind::Update, at, 1);
        } else if (op == 3) {
            const int added = 1 + static_cast<int>(rng() % 3);
            std::vector<Message> prefix;
            for (int i = 0; i < added; ++i)
                prefix.push_back(Message{next_id++, static_cast<int>(rng() % 2),
                                         static_cast<int>(rng() % 100), -1});
            model.messages.insert(model.messages.begin(), prefix.begin(), prefix.end());
            bump(model, TranscriptMutationKind::Prepend, 0,
                 static_cast<std::size_t>(added));
            if (model.facts.unread_first >= 0) model.facts.unread_first += added;
        } else if (op == 4) {
            model.facts.pane_width = model.facts.pane_width == 500.0f ? 360.0f : 500.0f;
        } else if (op == 5 && !model.messages.empty()) {
            const std::size_t at = rng() % model.messages.size();
            const int id = model.messages[at].id;
            if (model.expanded.count(id)) model.expanded.erase(id);
            else model.expanded.insert(id);
            ++model.facts.fold_revision;
            model.facts.fold_dirty_index = static_cast<int>(at);
        } else if (op == 6) {
            model.facts.show_date_dividers = !model.facts.show_date_dividers;
        } else if (!model.messages.empty()) {
            model.facts.unread_first = static_cast<int>(rng() % model.messages.size());
            model.facts.unread_count = static_cast<int>(model.messages.size()) -
                                       model.facts.unread_first;
        }
        check_reference(index, "0/random", model);
        const auto hit = update(index, "0/random", model);
        CHECK(hit.messages_visited == 0);
    }
}

int main() {
    test_unchanged_is_constant_work();
    test_bounded_and_pane_width_isolated();
    test_randomized_differential();
    if (failures == 0) {
        std::puts("OK");
        return 0;
    }
    std::printf("%d failure(s)\n", failures);
    return 1;
}
