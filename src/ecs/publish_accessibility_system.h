#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "../a11y_bridge.h"
#include "../ui/accessibility.h"
#include "components.h"
#include "ui_imports.h"

namespace ecs {

struct AccessibilityActionSystem : afterhours::System<UIContext<InputAction>> {
    void for_each_with(Entity&, UIContext<InputAction>&, float) override {
        const unsigned long long queued = native_a11y_take_pressed();
        if (queued == 0) return;
        const auto id = static_cast<afterhours::EntityID>(queued - 1ULL);
        auto opt = afterhours::ui::UICollectionHolder::getEntityForID(id);
        if (!opt.valid()) return;
        Entity& e = opt.asE();
        if (!e.has<afterhours::ui::HasClickListener>()) return;
        if (e.has<afterhours::ui::HasLabel>() &&
            e.get<afterhours::ui::HasLabel>().is_disabled)
            return;
        auto& listener = e.get<afterhours::ui::HasClickListener>();
        listener.down = true;
        if (listener.cb) listener.cb(e);
    }
};

struct PublishAccessibilitySystem : afterhours::System<UIContext<InputAction>> {
    void for_each_with(Entity&, UIContext<InputAction>& ctx, float) override {
        std::size_t signature = 1469598103934665603ULL;
        std::size_t seen = 0;
        const auto mix = [&signature](std::size_t v) {
            signature = (signature ^ v) * 1099511628211ULL;
        };

        for (const auto& e :
             afterhours::ui::UICollectionHolder::get().collection.get_entities()) {
            if (!e || !e->has<afterhours::ui::UIComponent>()) continue;
            const auto& uic = e->get<afterhours::ui::UIComponent>();
            if (!uic.was_rendered_to_screen || uic.should_hide) continue;
            Fingerprint fp;
            if (!fingerprint(*e, fp)) continue;
            const auto r = uic.rect();
            ++seen;
            mix(std::hash<std::string_view>{}(fp.value));
            mix(std::hash<std::string_view>{}(fp.parent));
            mix(static_cast<std::size_t>(fp.role));
            mix(static_cast<std::size_t>(
                (fp.enabled ? 1 : 0) | (fp.selected ? 2 : 0) |
                (fp.has_submenu ? 4 : 0) | (fp.expanded ? 8 : 0) |
                (ctx.focus_id == e->id ? 16 : 0)));
            mix(static_cast<std::size_t>(r.x) * 8191u +
                static_cast<std::size_t>(r.y) * 131u +
                static_cast<std::size_t>(r.width) * 7u +
                static_cast<std::size_t>(r.height));
        }

        if (signature == published_ && seen == publishedCount_) return;
        published_ = signature;
        publishedCount_ = seen;

        nodes_.clear();
        names_.clear();
        roles_.clear();
        parents_.clear();
        for (const auto& e :
             afterhours::ui::UICollectionHolder::get().collection.get_entities()) {
            if (!e || !e->has<afterhours::ui::UIComponent>()) continue;
            const auto& uic = e->get<afterhours::ui::UIComponent>();
            if (!uic.was_rendered_to_screen || uic.should_hide) continue;
            hanabi::a11y::AccessibleName derived;
            const hanabi::a11y::AccessibleName* named =
                describe_for(*e, derived);
            if (named == nullptr) continue;
            const auto& a = *named;
            const auto r = uic.rect();
            names_.push_back(a.value);
            roles_.push_back(hanabi::a11y::role_name(a.role));
            parents_.push_back(a.parent);
            NativeA11yNode n{};
            n.x = r.x;
            n.y = r.y;
            n.width = r.width;
            n.height = r.height;
            n.enabled = a.enabled ? 1 : 0;
            n.selected = a.selected ? 1 : 0;
            n.has_submenu = a.has_submenu ? 1 : 0;
            n.expanded = a.expanded ? 1 : 0;
            n.focused = ctx.focus_id == e->id ? 1 : 0;
            n.entity = static_cast<unsigned long long>(e->id) + 1ULL;
            nodes_.push_back(n);
        }

        for (std::size_t i = 0; i < nodes_.size(); ++i) {
            nodes_[i].name = names_[i].c_str();
            nodes_[i].role = roles_[i].c_str();
            nodes_[i].parent = parents_[i].c_str();
        }
        native_a11y_publish(nodes_.data(), nodes_.size());
    }

   private:
    struct Fingerprint {
        std::string_view value;
        std::string_view parent;
        hanabi::a11y::Role role = hanabi::a11y::Role::None;
        bool enabled = true;
        bool selected = false;
        bool has_submenu = false;
        bool expanded = false;
    };

    static bool fingerprint(const Entity& e, Fingerprint& out) {
        if (e.has<hanabi::a11y::AccessibleName>()) {
            const auto& a = e.get<hanabi::a11y::AccessibleName>();
            if (a.value.empty()) return false;
            out.value = a.value;
            out.parent = a.parent;
            out.role = a.role;
            out.enabled = a.enabled;
            out.selected = a.selected;
            out.has_submenu = a.has_submenu;
            out.expanded = a.expanded;
            return true;
        }
        if (!e.has<afterhours::ui::HasClickListener>()) return false;
        if (!e.has<afterhours::ui::UIComponentDebug>()) return false;
        const std::string& debug =
            e.get<afterhours::ui::UIComponentDebug>().name();
        if (debug.empty() || debug == "<unnamed>") return false;
        out.value = label_view(e, debug);
        out.role = e.has<afterhours::text_input::HasTextInputState>() ||
                           e.has<afterhours::text_input::HasTextAreaState>()
                       ? hanabi::a11y::Role::Field
                       : hanabi::a11y::Role::Button;
        out.enabled = !e.has<afterhours::ui::HasLabel>() ||
                      !e.get<afterhours::ui::HasLabel>().is_disabled;
        return true;
    }

    static std::string_view label_view(const Entity& e,
                                       const std::string& debug) {
        if (e.has<afterhours::ui::HasLabel>()) {
            const std::string& label = e.get<afterhours::ui::HasLabel>().label;
            if (!label.empty() && label != " ") return label;
        }
        return debug;
    }

    static const hanabi::a11y::AccessibleName* describe_for(
        const Entity& e, hanabi::a11y::AccessibleName& scratch) {
        if (e.has<hanabi::a11y::AccessibleName>()) {
            const auto& a = e.get<hanabi::a11y::AccessibleName>();
            return a.value.empty() ? nullptr : &a;
        }
        if (!e.has<afterhours::ui::HasClickListener>()) return nullptr;
        if (!e.has<afterhours::ui::UIComponentDebug>()) return nullptr;
        const std::string debug =
            e.get<afterhours::ui::UIComponentDebug>().name();
        if (debug.empty() || debug == "<unnamed>") return nullptr;
        scratch.value = spoken_label(e, debug);
        scratch.role = e.has<afterhours::text_input::HasTextInputState>() ||
                               e.has<afterhours::text_input::HasTextAreaState>()
                           ? hanabi::a11y::Role::Field
                           : hanabi::a11y::Role::Button;
        scratch.enabled = !e.has<afterhours::ui::HasLabel>() ||
                          !e.get<afterhours::ui::HasLabel>().is_disabled;
        return &scratch;
    }

    static std::string spoken_label(const Entity& e,
                                    const std::string& debug) {
        if (e.has<afterhours::ui::HasLabel>()) {
            const std::string& label = e.get<afterhours::ui::HasLabel>().label;
            if (!label.empty() && label != " ") return label;
        }
        std::string out = debug;
        for (char& c : out)
            if (c == '_') c = ' ';
        return out;
    }

    std::vector<NativeA11yNode> nodes_;
    std::vector<std::string> names_;
    std::vector<std::string> roles_;
    std::vector<std::string> parents_;
    std::size_t published_ = 0;
    std::size_t publishedCount_ = static_cast<std::size_t>(-1);
};

}  // namespace ecs
