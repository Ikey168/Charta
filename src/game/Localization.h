#pragma once

#include "core/Settings.h" // Settings (locale persistence, #61)

#include <map>
#include <string>
#include <vector>

/**
 * @file Localization.h
 * @brief String-table localization with fallback, formatting, and a completeness check (#373).
 *
 * Player-facing text was hard-coded at its use sites; this externalizes it. A StringTable maps
 * keys to localized strings; a Localizer holds the default (English) table plus additional
 * locales, resolves a key against the active locale with fallback to the default (and finally
 * the key itself, so nothing renders blank), formats parameterized strings ({name}/{count}),
 * switches locale at runtime, and persists the choice through Settings. A completeness check
 * reports keys missing from a locale or orphaned in it, so translation gaps are caught in CI.
 *
 * Header-only, std only (plus the header-only Settings). UI code calls loc.get(...) /
 * loc.format(...) instead of embedding literals.
 */
namespace IKore {
namespace game {

/// A single locale's key -> string map.
struct StringTable {
    std::string locale{"en"};
    std::map<std::string, std::string> entries;

    void set(const std::string& key, const std::string& value) { entries[key] = value; }
    bool has(const std::string& key) const { return entries.count(key) != 0; }
    std::string get(const std::string& key) const {
        auto it = entries.find(key);
        return it == entries.end() ? std::string() : it->second;
    }
};

class Localizer {
public:
    /// The default locale every lookup falls back to (usually English).
    void setDefault(const StringTable& table) {
        m_default = table;
        m_locales[table.locale] = table;
        if (m_current.empty()) m_current = table.locale;
    }
    /// Register (or replace) a locale.
    void addLocale(const StringTable& table) { m_locales[table.locale] = table; }

    bool hasLocale(const std::string& locale) const { return m_locales.count(locale) != 0; }
    const std::string& currentLocale() const { return m_current; }

    /// Switch the active locale at runtime; a no-op (returns false) for an unknown locale.
    bool setLocale(const std::string& locale) {
        if (!hasLocale(locale)) return false;
        m_current = locale;
        return true;
    }

    /// Resolve @p key against the active locale, then the default locale, then the key itself
    /// (so a missing string is visible, never blank).
    std::string get(const std::string& key) const {
        auto cur = m_locales.find(m_current);
        if (cur != m_locales.end() && cur->second.has(key)) return cur->second.get(key);
        if (m_default.has(key)) return m_default.get(key);
        return key;
    }

    /// get() with {name} placeholders substituted from @p args. Unreferenced placeholders are
    /// left as-is, and a placeholder with no matching arg is left intact.
    std::string format(const std::string& key, const std::map<std::string, std::string>& args) const {
        std::string s = get(key);
        for (const auto& kv : args) {
            const std::string token = "{" + kv.first + "}";
            std::size_t pos = 0;
            while ((pos = s.find(token, pos)) != std::string::npos) {
                s.replace(pos, token.size(), kv.second);
                pos += kv.second.size();
            }
        }
        return s;
    }

    // --- Completeness ---------------------------------------------------------

    /// Keys present in the default table but missing from @p locale (untranslated).
    std::vector<std::string> missingKeys(const std::string& locale) const {
        std::vector<std::string> out;
        auto it = m_locales.find(locale);
        if (it == m_locales.end()) return out;
        for (const auto& kv : m_default.entries)
            if (!it->second.has(kv.first)) out.push_back(kv.first);
        return out;
    }

    /// Keys present in @p locale but not in the default table (orphaned / stale).
    std::vector<std::string> orphanKeys(const std::string& locale) const {
        std::vector<std::string> out;
        auto it = m_locales.find(locale);
        if (it == m_locales.end()) return out;
        for (const auto& kv : it->second.entries)
            if (!m_default.has(kv.first)) out.push_back(kv.first);
        return out;
    }

    /// True if @p locale defines every default key and adds none of its own.
    bool isComplete(const std::string& locale) const {
        return missingKeys(locale).empty() && orphanKeys(locale).empty();
    }

    // --- Settings persistence (#61) ------------------------------------------

    void saveLocale(Settings& settings, const std::string& key = "ui.locale") const {
        settings.setString(key, m_current);
    }
    /// Load and apply a persisted locale; falls back to the current locale if unknown.
    void loadLocale(const Settings& settings, const std::string& key = "ui.locale") {
        const std::string stored = settings.getString(key, m_current);
        setLocale(stored); // no-op if the stored locale is not registered
    }

private:
    StringTable m_default;
    std::map<std::string, StringTable> m_locales;
    std::string m_current;
};

/// The default English UI strings the HUD/menus render through (keys, not literals).
inline StringTable defaultUiStrings() {
    StringTable t;
    t.locale = "en";
    t.set("hud.coins", "Coins: {count}");
    t.set("hud.score", "Score: {score}");
    t.set("hud.keys", "Keys: {count}");
    t.set("menu.play", "Play");
    t.set("menu.settings", "Settings");
    t.set("menu.quit", "Quit");
    t.set("prompt.win", "You win!");
    t.set("prompt.lose", "You lose");
    t.set("prompt.pause", "Paused");
    return t;
}

} // namespace game
} // namespace IKore
