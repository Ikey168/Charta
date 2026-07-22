// Single version source - issue #376.
//
// The generated version header (from cmake/Version.h.in, driven by project(VERSION ...) and
// IKORE_RELEASE_VERSION) is the one place the version is defined. This checks the numeric
// components, that the semver string starts with them and is well-formed, and that the
// product name is set - so the build stamp, packaging, and in-app about all agree.
//   Built by CMake with build/generated on the include path.

#include "ikore/Version.h"

#include <cctype>
#include <cstdio>
#include <string>

using namespace IKore::version;

static int g_failures = 0;

#define CHECK(cond)                                               \
    do {                                                          \
        if (!(cond)) {                                            \
            std::printf("FAIL (line %d): %s\n", __LINE__, #cond); \
            ++g_failures;                                         \
        }                                                         \
    } while (0)

// Parse "MAJOR.MINOR.PATCH" (optionally followed by -prerelease/+build) and check it matches
// the numeric constants.
static bool parseSemverPrefix(const std::string& s, int& maj, int& min, int& pat) {
    std::size_t i = 0;
    auto num = [&](int& out) -> bool {
        std::size_t start = i;
        while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) ++i;
        if (i == start) return false;
        out = std::stoi(s.substr(start, i - start));
        return true;
    };
    if (!num(maj)) return false;
    if (i >= s.size() || s[i] != '.') return false;
    ++i;
    if (!num(min)) return false;
    if (i >= s.size() || s[i] != '.') return false;
    ++i;
    if (!num(pat)) return false;
    // What follows must be end, '-' (pre-release), or '+' (build metadata).
    return i == s.size() || s[i] == '-' || s[i] == '+';
}

int main() {
    // 1. Numeric components are the 1.0.x line.
    CHECK(kMajor == 1);
    CHECK(kMinor == 0);
    CHECK(kPatch == 1);

    // 2. The version string is well-formed semver and its numeric prefix matches the
    //    constants (single source of truth: string and numbers cannot drift).
    const std::string v = kString;
    std::printf("[info] version = %s (%s)\n", kString, kProduct);
    CHECK(!v.empty());
    int maj = -1, min = -1, pat = -1;
    CHECK(parseSemverPrefix(v, maj, min, pat));
    CHECK(maj == kMajor && min == kMinor && pat == kPatch);

    // 3. The product name is set (shown alongside the version in the about screen).
    CHECK(std::string(kProduct).size() > 0);

    if (g_failures == 0) {
        std::printf("test_version: all checks passed\n");
        return 0;
    }
    std::printf("test_version: %d check(s) failed\n", g_failures);
    return 1;
}
