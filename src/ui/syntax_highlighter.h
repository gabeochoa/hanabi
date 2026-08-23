#pragma once

// ---------------------------------------------------------------------------
// Syntax colouring for fenced code blocks.
//
// A fenced block is its OWN row per line — a mono label with no wrapping — so
// per-run colour reaches it through `with_styled_label`. Inline mono inside a
// paragraph is a different story and is not attempted here: a `TextSpan`
// carries colour and weight but no per-run FONT, so an inline run cannot be
// drawn in the mono face at all (afterhours_gaps.md, the #22 follow-up).
//
// This is a SCANNER, not a parser: it knows about comments, strings, numbers
// and a keyword list per language, and nothing about grammar. That is a
// deliberate ceiling — a code block in a chat transcript is usually a fragment
// with no valid parse, and a scanner degrades into "mostly right" where a
// parser degrades into nothing. What it cannot do it leaves as plain text.
//
// State carries ACROSS lines, because a block comment and a Python docstring
// do. It is per code block, and the caller resets it at the opening fence.
// ---------------------------------------------------------------------------

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace hanabi::syntax {

enum class Tok {
    Plain,
    Keyword,   // control flow, declarations, the words the language owns
    Type,      // built-in type names and literals (int, true, nil)
    String,    // quoted runs, including their quotes
    Comment,   // to end of line, or a whole block comment
    Number,    // numeric literals
    Punct,     // brackets and operators
};

enum class Lang {
    None,
    C,       // C, C++, Java, Go, Rust, JS, TS — the //-and-/* */ family
    Python,  // # comments, triple-quoted strings
    Shell,   // # comments, no block comments
    Sql,     // -- comments
    Yaml,    // # comments, key: value
    Json,    // no comments, everything is a literal
};

// The fence tag, upper-cased by the caller, mapped to a scanner. An unknown
// tag gets Lang::None, which colours nothing — an honest "I don't know this
// one" rather than colouring another language's keywords over it.
inline Lang lang_from_tag(std::string_view tag) {
    if (tag == "PY" || tag == "PYTHON") return Lang::Python;
    if (tag == "SH" || tag == "BASH" || tag == "ZSH" || tag == "SHELL" ||
        tag == "CONSOLE")
        return Lang::Shell;
    if (tag == "SQL") return Lang::Sql;
    if (tag == "YAML" || tag == "YML") return Lang::Yaml;
    if (tag == "JSON") return Lang::Json;
    if (tag == "C" || tag == "CPP" || tag == "C++" || tag == "CC" ||
        tag == "H" || tag == "HPP" || tag == "OBJC" || tag == "JAVA" ||
        tag == "GO" || tag == "RUST" || tag == "RS" || tag == "JS" ||
        tag == "JSX" || tag == "TS" || tag == "TSX" ||
        tag == "JAVASCRIPT" || tag == "TYPESCRIPT" || tag == "KT" ||
        tag == "KOTLIN" || tag == "SWIFT" || tag == "CS")
        return Lang::C;
    return Lang::None;
}

struct Run {
    size_t off = 0;
    size_t len = 0;
    Tok tok = Tok::Plain;
};

// What a line inherits from the line above it.
struct State {
    bool in_block_comment = false;  // inside /* … */
    char in_long_string = '\0';     // inside a Python ''' or """ run
};

namespace detail {

inline bool word_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '_' || c == '$';
}
inline bool digit(char c) { return c >= '0' && c <= '9'; }

// One shared keyword table, keyed by language. Each list is the words that
// carry meaning at a glance — the ones a reader scans for — not the full
// grammar. Sorted only for reading; lookup is linear over a short list.
inline bool in_list(std::string_view w, const std::vector<std::string_view>& l) {
    for (std::string_view k : l)
        if (k == w) return true;
    return false;
}

inline const std::vector<std::string_view>& c_keywords() {
    static const std::vector<std::string_view> k = {
        "auto", "break", "case", "catch", "class", "const", "constexpr",
        "continue", "default", "defer", "delete", "do", "else", "enum",
        "export", "extends", "extern", "final", "finally", "fn", "for",
        "func", "function", "go", "if", "impl", "implements", "import",
        "in", "inline", "interface", "let", "match", "mod", "mut", "namespace",
        "new", "operator", "package", "private", "protected", "pub", "public",
        "range", "return", "select", "static", "struct", "super", "switch",
        "template", "this", "throw", "trait", "try", "type", "typedef",
        "typename", "union", "unsafe", "use", "using", "var", "virtual",
        "where", "while", "yield", "async", "await", "const_cast",
        "static_cast", "dynamic_cast", "reinterpret_cast", "throws",
        "instanceof", "extends", "abstract", "override", "declare", "readonly",
    };
    return k;
}
inline const std::vector<std::string_view>& c_types() {
    static const std::vector<std::string_view> k = {
        "bool", "boolean", "byte", "char", "double", "float", "int", "int8",
        "int16", "int32", "int64", "uint", "uint8", "uint16", "uint32",
        "uint64", "long", "short", "signed", "unsigned", "size_t", "string",
        "str", "String", "void", "true", "false", "null", "nullptr", "nil",
        "None", "undefined", "self", "Self", "number", "any", "unknown",
        "never", "u8", "u16", "u32", "u64", "usize", "i8", "i16", "i32",
        "i64", "isize", "f32", "f64", "error", "Option", "Result", "Some",
        "Ok", "Err", "vec", "Vec",
    };
    return k;
}
inline const std::vector<std::string_view>& py_keywords() {
    static const std::vector<std::string_view> k = {
        "and", "as", "assert", "async", "await", "break", "class", "continue",
        "def", "del", "elif", "else", "except", "finally", "for", "from",
        "global", "if", "import", "in", "is", "lambda", "nonlocal", "not",
        "or", "pass", "raise", "return", "try", "while", "with", "yield",
        "match", "case",
    };
    return k;
}
inline const std::vector<std::string_view>& py_types() {
    static const std::vector<std::string_view> k = {
        "True", "False", "None", "self", "cls", "int", "str", "float", "bool",
        "list", "dict", "set", "tuple", "bytes", "len", "print", "range",
        "type", "object", "super",
    };
    return k;
}
inline const std::vector<std::string_view>& sh_keywords() {
    static const std::vector<std::string_view> k = {
        "if", "then", "elif", "else", "fi", "for", "while", "until", "do",
        "done", "case", "esac", "function", "in", "return", "exit", "export",
        "local", "readonly", "set", "unset", "shift", "source", "trap",
    };
    return k;
}
inline const std::vector<std::string_view>& sh_types() {
    static const std::vector<std::string_view> k = {
        "echo", "cd", "ls", "grep", "sed", "awk", "cat", "make", "git",
        "cargo", "python3", "sudo", "rm", "cp", "mv", "mkdir", "test",
    };
    return k;
}
inline const std::vector<std::string_view>& sql_keywords() {
    static const std::vector<std::string_view> k = {
        "select", "from", "where", "group", "by", "order", "having", "join",
        "left", "right", "inner", "outer", "full", "on", "as", "and", "or",
        "not", "in", "is", "null", "insert", "into", "values", "update",
        "set", "delete", "create", "table", "drop", "alter", "index", "view",
        "with", "union", "all", "distinct", "limit", "offset", "case", "when",
        "then", "else", "end", "asc", "desc", "count", "sum", "avg", "min",
        "max",
    };
    return k;
}
inline const std::vector<std::string_view>& json_types() {
    static const std::vector<std::string_view> k = {"true", "false", "null"};
    return k;
}

// SQL keywords are matched case-insensitively; every other language's are not.
inline std::string lower_of(std::string_view w) {
    std::string s;
    s.reserve(w.size());
    for (char c : w)
        s.push_back((c >= 'A' && c <= 'Z') ? static_cast<char>(c + 32) : c);
    return s;
}

}  // namespace detail

// Scan one line. Returns the runs that carry colour, in order, covering only
// the parts that are NOT plain (the caller fills the gaps with the block's
// ordinary text colour).
inline std::vector<Run> scan(Lang lang, const std::string& line, State& st) {
    std::vector<Run> out;
    if (lang == Lang::None) return out;

    const size_t n = line.size();
    size_t i = 0;

    // A block comment or a long string that opened on an earlier line owns the
    // start of this one.
    if (st.in_block_comment) {
        const size_t close = line.find("*/");
        const size_t end = (close == std::string::npos) ? n : close + 2;
        if (end > 0) out.push_back(Run{0, end, Tok::Comment});
        if (close == std::string::npos) return out;
        st.in_block_comment = false;
        i = end;
    } else if (st.in_long_string != '\0') {
        const std::string delim(3, st.in_long_string);
        const size_t close = line.find(delim);
        const size_t end = (close == std::string::npos) ? n : close + 3;
        if (end > 0) out.push_back(Run{0, end, Tok::String});
        if (close == std::string::npos) return out;
        st.in_long_string = '\0';
        i = end;
    }

    const bool cLike = (lang == Lang::C);
    const bool hashComment =
        (lang == Lang::Python || lang == Lang::Shell || lang == Lang::Yaml);

    while (i < n) {
        const char c = line[i];

        // ---- comments --------------------------------------------------
        if (hashComment && c == '#') {
            out.push_back(Run{i, n - i, Tok::Comment});
            break;
        }
        if (cLike && c == '/' && i + 1 < n && line[i + 1] == '/') {
            out.push_back(Run{i, n - i, Tok::Comment});
            break;
        }
        if (lang == Lang::Sql && c == '-' && i + 1 < n && line[i + 1] == '-') {
            out.push_back(Run{i, n - i, Tok::Comment});
            break;
        }
        if (cLike && c == '/' && i + 1 < n && line[i + 1] == '*') {
            const size_t close = line.find("*/", i + 2);
            const size_t end = (close == std::string::npos) ? n : close + 2;
            out.push_back(Run{i, end - i, Tok::Comment});
            if (close == std::string::npos) {
                st.in_block_comment = true;
                break;
            }
            i = end;
            continue;
        }

        // ---- strings ---------------------------------------------------
        if (c == '"' || c == '\'' || (cLike && c == '`')) {
            // Python's triple quote runs until its partner, however many
            // lines away that is.
            if (lang == Lang::Python && i + 2 < n && line[i + 1] == c &&
                line[i + 2] == c) {
                const std::string delim(3, c);
                const size_t close = line.find(delim, i + 3);
                const size_t end = (close == std::string::npos) ? n : close + 3;
                out.push_back(Run{i, end - i, Tok::String});
                if (close == std::string::npos) {
                    st.in_long_string = c;
                    break;
                }
                i = end;
                continue;
            }
            size_t j = i + 1;
            while (j < n) {
                if (line[j] == '\\' && j + 1 < n) {
                    j += 2;
                    continue;
                }
                if (line[j] == c) {
                    ++j;
                    break;
                }
                ++j;
            }
            out.push_back(Run{i, j - i, Tok::String});
            i = j;
            continue;
        }

        // ---- numbers ---------------------------------------------------
        if (detail::digit(c) &&
            (i == 0 || !detail::word_char(line[i - 1]))) {
            size_t j = i;
            while (j < n && (detail::word_char(line[j]) || line[j] == '.'))
                ++j;
            out.push_back(Run{i, j - i, Tok::Number});
            i = j;
            continue;
        }

        // ---- words -----------------------------------------------------
        if (detail::word_char(c)) {
            size_t j = i;
            while (j < n && detail::word_char(line[j])) ++j;
            const std::string_view w(line.data() + i, j - i);
            Tok tok = Tok::Plain;
            switch (lang) {
                case Lang::C:
                    if (detail::in_list(w, detail::c_keywords()))
                        tok = Tok::Keyword;
                    else if (detail::in_list(w, detail::c_types()))
                        tok = Tok::Type;
                    break;
                case Lang::Python:
                    if (detail::in_list(w, detail::py_keywords()))
                        tok = Tok::Keyword;
                    else if (detail::in_list(w, detail::py_types()))
                        tok = Tok::Type;
                    break;
                case Lang::Shell:
                    if (detail::in_list(w, detail::sh_keywords()))
                        tok = Tok::Keyword;
                    else if (detail::in_list(w, detail::sh_types()))
                        tok = Tok::Type;
                    break;
                case Lang::Sql: {
                    const std::string lw = detail::lower_of(w);
                    if (detail::in_list(lw, detail::sql_keywords()))
                        tok = Tok::Keyword;
                    break;
                }
                case Lang::Json:
                    if (detail::in_list(w, detail::json_types()))
                        tok = Tok::Type;
                    break;
                case Lang::Yaml:
                    // A YAML key is whatever sits before the colon at this
                    // indent level; the word list is the document's, not the
                    // language's, so the key is what gets the colour.
                    if (j < n && line[j] == ':') tok = Tok::Keyword;
                    break;
                case Lang::None:
                    break;
            }
            if (tok != Tok::Plain) out.push_back(Run{i, j - i, tok});
            i = j;
            continue;
        }

        // Punctuation is deliberately NOT coloured. Each span is measured and
        // placed on its own, so every run boundary costs a fraction of a pixel
        // of drift (afterhours_gaps.md #59) — and in a monospace block that
        // drift is visible as a wobbling column. Punctuation is the most
        // frequent boundary there is, so colouring it bought a hue and cost
        // the alignment that makes code readable.
        ++i;
    }
    return out;
}

}  // namespace hanabi::syntax
