#include "Test.h"
#include "OptimizedSearch.h"
#include <string>
#include <vector>

static std::vector<std::size_t> ref_find_all(const std::string &text, const std::string &pat) {
    std::vector<std::size_t> res;
    if (pat.empty()) return res;
    std::size_t from = 0;
    while (true) {
        auto p = text.find(pat, from);
        if (p == std::string::npos) break;
        res.push_back(p);
        from = p + pat.size();
    }
    return res;
}

TEST(OptimizedSearch_basic_cases) {
    OptimizedSearch os;
    struct Case { std::string text; std::string pat; } cases[] = {
        {"", ""},
        {"", "a"},
        {"a", ""},
        {"a", "a"},
        {"aaaaa", "aa"},
        {"hello world", "world"},
        {"abcabcabc", "abc"},
        {"the quick brown fox", "fox"},
    };
    for (auto &c : cases) {
        auto got = os.find_all(c.text, c.pat, 0);
        auto ref = ref_find_all(c.text, c.pat);
        ASSERT_EQ(got, ref);
    }
}
