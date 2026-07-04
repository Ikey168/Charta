// Localization scaffold and string table - issue #373.
//
// Verifies: lookups resolve through the active locale, a missing key falls back to the
// default locale (and finally to the key itself), parameterized strings format from named
// args, the completeness check flags missing and orphaned keys, locale switches at runtime,
// and the choice persists via settings. Pure std + header-only:
//   g++ -std=c++17 -I src tests/test_localization.cpp -o test_localization

#include "game/Localization.h"

#include <algorithm>
#include <cstdio>
#include <string>

using namespace IKore;
using namespace IKore::game;

static int g_failures = 0;

#define CHECK(cond)                                               \
    do {                                                          \
        if (!(cond)) {                                            \
            std::printf("FAIL (line %d): %s\n", __LINE__, #cond); \
            ++g_failures;                                         \
        }                                                         \
    } while (0)

static bool contains(const std::vector<std::string>& v, const std::string& s) {
    return std::find(v.begin(), v.end(), s) != v.end();
}

int main() {
    Localizer loc;
    loc.setDefault(defaultUiStrings()); // en, current locale

    // A Spanish locale: some keys translated, one default key missing, one orphan key added.
    StringTable es;
    es.locale = "es";
    es.set("menu.play", "Jugar");
    es.set("menu.settings", "Opciones");
    es.set("prompt.win", "Ganaste");
    es.set("hud.coins", "Monedas: {count}");
    es.set("es.only", "solo"); // orphan: not in the default table
    // (deliberately omits menu.quit, prompt.lose, etc.)
    loc.addLocale(es);

    // 1. Lookups resolve in the active locale (default = en to start).
    CHECK(loc.currentLocale() == "en");
    CHECK(loc.get("menu.play") == "Play");

    // 2. Runtime switch changes resolution; unknown locale is a no-op.
    CHECK(loc.setLocale("es"));
    CHECK(loc.get("menu.play") == "Jugar");
    CHECK(!loc.setLocale("de")); // not registered
    CHECK(loc.currentLocale() == "es");

    // 3. A key missing from the active locale falls back to the default; a key missing from
    //    both falls back to itself (never blank).
    CHECK(loc.get("menu.quit") == "Quit");                 // es lacks it -> en default
    CHECK(loc.get("totally.unknown") == "totally.unknown"); // absent everywhere

    // 4. Parameterized formatting substitutes named args.
    CHECK(loc.format("hud.coins", {{"count", "7"}}) == "Monedas: 7");
    loc.setLocale("en");
    CHECK(loc.format("hud.coins", {{"count", "12"}}) == "Coins: 12");
    // An unreferenced placeholder is left intact.
    CHECK(loc.format("hud.score", {}) == "Score: {score}");

    // 5. Completeness check flags the deliberate gaps.
    const std::vector<std::string> missing = loc.missingKeys("es");
    CHECK(contains(missing, "menu.quit"));   // untranslated
    CHECK(contains(missing, "prompt.lose"));
    const std::vector<std::string> orphans = loc.orphanKeys("es");
    CHECK(contains(orphans, "es.only"));     // stale/extra
    CHECK(!loc.isComplete("es"));

    // A fully-mirrored locale is complete.
    StringTable full;
    full.locale = "fr";
    for (const auto& kv : defaultUiStrings().entries) full.set(kv.first, kv.second + " (fr)");
    loc.addLocale(full);
    CHECK(loc.isComplete("fr"));
    CHECK(loc.missingKeys("fr").empty() && loc.orphanKeys("fr").empty());

    // 6. Locale selection persists through settings.
    loc.setLocale("es");
    Settings settings;
    loc.saveLocale(settings);
    Settings reloaded;
    reloaded.load(settings.serialize());
    Localizer loc2;
    loc2.setDefault(defaultUiStrings());
    loc2.addLocale(es);
    loc2.loadLocale(reloaded);
    CHECK(loc2.currentLocale() == "es");
    CHECK(loc2.get("menu.play") == "Jugar");

    if (g_failures == 0) {
        std::printf("test_localization: all checks passed\n");
        return 0;
    }
    std::printf("test_localization: %d check(s) failed\n", g_failures);
    return 1;
}
