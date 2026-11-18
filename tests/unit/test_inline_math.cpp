#include <cstdio>
#include <string>

#include "../../src/ui/display_math.h"
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

    // Display math (kt-heop, the reference 2026-10-14): $$...$$ and
    // \[...\] blocks are found whole, typeset, and never left as raw
    // dollar signs; unsupported source is preserved without delimiters.
    {
        using namespace hanabi::display_math;
        const std::string body = "before\n$$\nx^2 + y^2 = z^2\n$$\nafter";
        const auto found = block_at(body, 7);
        CHECK(found && found->source == "x^2 + y^2 = z^2");
        CHECK(found && body.compare(found->after, std::string::npos, "after") == 0);
        CHECK(!block_at(body, 0));
        CHECK(!block_at("$$\nx^2", 0));  // unclosed: not a block
        const auto enclosed = block_at("$$x^2 + y^2 = z^2$$", 0);
        CHECK(enclosed && enclosed->source == "x^2 + y^2 = z^2");
        const auto brackets = block_at("\\[\n\\alpha + \\beta\n\\]", 0);
        CHECK(brackets && brackets->source == "\\alpha + \\beta");

        const auto aligned = lines("\\begin{aligned}\nJ &= \\text{Sensitivity} + "
                                   "\\text{Specificity} - 1 \\\\\n&= \\text{Recall} - x\n\\end{aligned}");
        CHECK(aligned && aligned->size() == 2);
        CHECK(aligned && (*aligned)[0].left == "J ");
        CHECK(aligned && (*aligned)[0].right == "= Sensitivity + Specificity \xe2\x88\x92 1");
        CHECK(aligned && (*aligned)[1].right == "= Recall \xe2\x88\x92 x");
        const auto relations = lines("x <= 5 \\\\ y >= 3");
        CHECK(relations && relations->size() == 2 && (*relations)[0].right == "<= 5");
        CHECK(lines("\\foo = 1") == std::nullopt);

        bool was_typeset = false;
        const auto drawn = rendered_lines("x^2 + y^2 = z^2", hanabi::inline_math::every_glyph, &was_typeset);
        CHECK(was_typeset && drawn.size() == 1 && drawn[0] == "x\xc2\xb2 + y\xc2\xb2 = z\xc2\xb2");
        CHECK(drawn[0].find('$') == std::string::npos);
        const auto kept = rendered_lines("\\foo{bar}", hanabi::inline_math::every_glyph, &was_typeset);
        CHECK(!was_typeset && kept.size() == 1 && kept[0] == "\\foo{bar}");
    }

    if (g_failures == 0) std::printf("OK\n");
    return g_failures == 0 ? 0 : 1;
}
