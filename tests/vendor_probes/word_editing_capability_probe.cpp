#define FMT_HEADER_ONLY
#include <afterhours/src/plugins/ui/text_input/utils.h>

enum struct CompleteActions {
    TextWordLeft,
    TextWordRight,
    TextDeleteWordBack,
    TextDeleteWordForward,
};

enum struct IncompleteActions {
    TextWordLeft,
    TextWordRight,
};

static_assert(afterhours::text_input::supports_word_editing<CompleteActions>);
static_assert(
    !afterhours::text_input::supports_word_editing<IncompleteActions>);

int main() { return 0; }
