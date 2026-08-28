#include <fstream>
#include <iostream>
#include <string>

std::string read_all(const char *path) {
    std::ifstream in(path);
    return {std::istreambuf_iterator<char>(in),
            std::istreambuf_iterator<char>()};
}

bool ordered(const std::string &text,
             std::initializer_list<std::string> needles) {
    std::size_t at = 0;
    for (const auto &needle : needles) {
        at = text.find(needle, at);
        if (at == std::string::npos) return false;
        at += needle.size();
    }
    return true;
}

int main(int argc, char **argv) {
    if (argc != 3) return 2;
    const std::string kind = argv[1];
    const std::string text = read_all(argv[2]);
    if (kind == "atlas") {
        return ordered(text, {"handle_fontstash_error", "FONS_ATLAS_FULL",
                              "static bool reported",
                              "fonsSetErrorCallback(g_fons_ctx, "
                              "handle_fontstash_error, nullptr)"})
                   ? 0
                   : 1;
    }
    if (kind == "sampler") {
        const auto begin =
            text.find("sg_sampler smp = make_sampler_for_filter(filter);");
        const auto end = text.find("TextureType tex{};", begin);
        if (begin == std::string::npos || end == std::string::npos) return 1;
        const auto block = text.substr(begin, end - begin);
        return ordered(block,
                       {"sg_query_sampler_state(smp)",
                        "sg_destroy_sampler(smp)", "sg_destroy_view(view)",
                        "sg_destroy_image(img)", "return TextureType{};"})
                   ? 0
                   : 1;
    }
    std::cerr << "unknown contract: " << kind << '\n';
    return 2;
}
