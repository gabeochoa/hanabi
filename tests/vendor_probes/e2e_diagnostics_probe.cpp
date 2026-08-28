#define FMT_HEADER_ONLY
#include <afterhours/ah.h>
#include <afterhours/src/plugins/e2e_testing/command_handlers.h>
#include <afterhours/src/plugins/e2e_testing/ui_commands.h>
#include <afterhours/src/plugins/e2e_testing/visible_text.h>

#include <string>

using afterhours::Entity;
using afterhours::testing::E2ECommandCleanupSystem;
using afterhours::testing::PendingE2ECommand;
using afterhours::testing::VisibleTextRegistry;

enum struct Action {
    None,
    WidgetMod,
    WidgetNext,
    WidgetBack,
    WidgetPress,
    WidgetUp,
    WidgetDown,
    WidgetLeft,
    WidgetRight,
};

std::string timeout_for(std::string name, std::vector<std::string> args) {
    Entity entity;
    PendingE2ECommand command;
    command.name = std::move(name);
    command.args = std::move(args);
    E2ECommandCleanupSystem cleanup;
    for (int i = 0; i <= PendingE2ECommand::MAX_FRAMES; ++i)
        cleanup.for_each_with(entity, command, 0.f);
    return command.error_message;
}

int main() {
    afterhours::SystemManager systems;
    afterhours::testing::ui_commands::register_ui_commands<Action>(systems);
    int dump_handlers = 0;
    for (const auto &system : systems.update_systems_)
        dump_handlers +=
            dynamic_cast<
                afterhours::testing::ui_commands::HandleDumpUICommand *>(
                system.get()) != nullptr;

    const auto subject = timeout_for("assert_ui", {"missing_widget", "x=1"});
    auto &visible = VisibleTextRegistry::instance();
    visible.clear();
    visible.register_text(std::string(260, 'x') + "TAIL");
    const auto evidence = timeout_for("expect_text", {"needle"});

    return dump_handlers == 1 &&
                   subject.find("missing_widget") != std::string::npos &&
                   evidence.find("TAIL") != std::string::npos
               ? 0
               : 1;
}
