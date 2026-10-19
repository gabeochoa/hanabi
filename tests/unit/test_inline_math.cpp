#include <cstdio>
#include <string>

#include "../../src/ui/inline_math.h"
#include <afterhours/src/font_coverage.h>

static int g_failures = 0;
#define CHECK(cond)                                                    \
    do {                                                               \
        if (!(cond)) {                                                 \
            std::printf("  FAIL: %s (line %d)\n", #cond, __LINE__);    \
            ++g_failures;                                              \
        }                                                              \
    } while (0)

int main() {
    using hanabi::inline_math::closing_dollar;
    using hanabi::inline_math::rewritten;
    using hanabi::inline_math::typeset;

    CHECK(typeset("f") == "f");
    CHECK(typeset("2\\pi") == "2π");
    CHECK(typeset("x_1^2") == "x₁²");
    CHECK(typeset("\\alpha + \\beta") == "α + β");
    CHECK(typeset("\\frac{a}{b}") == "a/b");
    CHECK(typeset("\\frac{a+b}{2}") == "(a+b)/2");
    CHECK(typeset("\\sqrt{2}") == "√2");
    CHECK(typeset("\\sqrt{x+1}") == "√(x+1)");
    CHECK(typeset("x^{10}") == "x¹⁰");
    CHECK(typeset("x^{ab}") == "x^(ab)");
    CHECK(typeset("e^{i\\pi}") == "e^(iπ)");
    CHECK(typeset("\\mathrm{d}x") == "dx");
    CHECK(typeset("\\left(a\\right)") == "(a)");
    CHECK(typeset("a \\le b") == "a ≤ b");
    CHECK(typeset("\\sum_{i=1}^n") == "∑ᵢ₌₁ⁿ");
    CHECK(typeset("\\foo").empty());
    CHECK(typeset("a & b").empty());
    CHECK(typeset("#tag").empty());
    CHECK(typeset("a__b").empty());
    CHECK(typeset("{a").empty());
    CHECK(typeset("a}").empty());
    CHECK(typeset("a-b") == "a-b");
    CHECK(typeset("a-b", hanabi::inline_math::every_glyph, true) == "a−b");

    const std::string prose = "x is $f$ here";
    CHECK(closing_dollar(prose, 5) == 7);
    CHECK(closing_dollar("costs $5 and $10 today", 6) == std::string::npos);
    CHECK(closing_dollar("$50-$100", 0) == std::string::npos);
    CHECK(closing_dollar("echo $1 $2", 5) == std::string::npos);
    CHECK(closing_dollar("$HOME/$USER", 0) == std::string::npos);
    CHECK(closing_dollar("${a}${b}", 0) == std::string::npos);
    CHECK(closing_dollar("a$b$", 1) == std::string::npos);
    CHECK(closing_dollar("$a b$", 0) == 4);
    CHECK(closing_dollar("$a $", 0) == std::string::npos);
    CHECK(closing_dollar("$a\nb$", 0) == std::string::npos);

    CHECK(rewritten("plain prose") == "plain prose");
    CHECK(rewritten("let $f$ be") == "let *f* be");
    CHECK(rewritten("area $2\\pi r$ total") == "area *2π r* total");
    CHECK(rewritten("costs $5 and $10 today") == "costs $5 and $10 today");
    CHECK(rewritten("run `echo $x$` first") == "run `echo $x$` first");
    CHECK(rewritten("$$a$$ stays") == "$$a$$ stays");
    CHECK(rewritten("```\n$f$\n```\n$f$") == "```\n$f$\n```\n*f*");
    CHECK(rewritten("$x_1$ then $x_2$") == "*x₁* then *x₂*");
    CHECK(rewritten("$a & b$ stays") == "$a & b$ stays");
    CHECK(rewritten("$a*b$") == "*a\\*b*");
    CHECK(rewritten("$5 and $x$") == "$5 and *x*");

    afterhours::font_coverage::Ranges roboto;
    afterhours::font_coverage::Ranges atkinson;
    CHECK(afterhours::font_coverage::parse_file("resources/fonts/Roboto-Regular.ttf", roboto));
    CHECK(afterhours::font_coverage::parse_file("resources/fonts/AtkinsonHyperlegible-Regular.ttf", atkinson));
    const hanabi::inline_math::GlyphCheck inRoboto = [&](std::uint32_t cp) {
        return afterhours::font_coverage::covers(roboto, cp);
    };
    const hanabi::inline_math::GlyphCheck inAtkinson = [&](std::uint32_t cp) {
        return afterhours::font_coverage::covers(atkinson, cp);
    };
    CHECK(typeset("2\\pi", inRoboto) == "2π");
    CHECK(typeset("2\\pi", inAtkinson) == "2π");
    CHECK(typeset("\\alpha", inAtkinson).empty());
    CHECK(typeset("x^2", inRoboto) == "x²");
    CHECK(typeset("x_1", inRoboto) == "x_1");
    CHECK(typeset("x^{10}", inRoboto) == "x^(10)");
    CHECK(typeset("a \\to b", inRoboto).empty());
    CHECK(typeset("a \\le b", inRoboto) == "a ≤ b");
    CHECK(typeset("x \\in S", inRoboto).empty());
    CHECK(typeset("\\sqrt{2}", inRoboto) == "√2");
    CHECK(rewritten("so $x \\to y$ holds", inRoboto) == "so $x \\to y$ holds");
    CHECK(rewritten("so $x \\le y$ holds", inRoboto) == "so *x ≤ y* holds");

    if (g_failures == 0) std::printf("OK\n");
    return g_failures == 0 ? 0 : 1;
}
