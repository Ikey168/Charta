#pragma once

#include "game/LevelShare.h" // encodeShare/decodeShare + detail::fnv1a64/base64 (#317)

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

/**
 * @file SaveSync.h
 * @brief Unified save model, corruption-resistant local writes, and cloud sync (issue #370).
 *
 * One coherent persistence layer over the scattered pieces (local save/config #58, level
 * persistence #241, share codes #317): a player's profile, progress, settings, authored
 * levels, and saved replays, serialized with a version header and forward-compatible,
 * tolerant parsing so old saves migrate and future fields do not break an older build.
 *
 *   - SaveData: the whole model; serialize()/deserialize() round-trip and migrate.
 *   - frameSave()/unframeSave(): a checksum envelope so a truncated or corrupt blob is
 *     detected; SaveManager recovers to a valid default rather than loading garbage, and
 *     SaveStorage writes atomically (temp-write-then-rename for the file backend).
 *   - syncLastWriterWins()/CloudSync: reconcile a divergent local/remote pair
 *     deterministically behind a storage interface, so a real backend drops in later.
 *   - Authored levels and replays are carried as #317 share strings, so the same code
 *     path shares and imports them.
 *
 * Header-only, std only (plus the header-only LevelShare). Behind the SaveStorage
 * interface it is fully headless-testable.
 */
namespace IKore {
namespace game {

constexpr int kSaveVersion = 2;

/// Per-level progress. bestTimeMs was added in v2; a v1 save migrates it to 0.
struct LevelProgressEntry {
    int bestStars{0};
    int bestTimeMs{0};

    bool operator==(const LevelProgressEntry& o) const {
        return bestStars == o.bestStars && bestTimeMs == o.bestTimeMs;
    }
};

/// The unified save: profile, progress, settings, authored levels, saved replays.
struct SaveData {
    int version{kSaveVersion};
    std::string profile;
    std::map<std::string, LevelProgressEntry> progress; ///< levelId -> progress
    std::map<std::string, std::string> settings;        ///< settings kv
    std::vector<std::string> authoredLevels;            ///< #317 share strings
    std::vector<std::string> savedReplays;              ///< #317 share strings
    std::uint64_t updatedAt{0};                         ///< logical clock for last-writer-wins

    bool operator==(const SaveData& o) const {
        return version == o.version && profile == o.profile && progress == o.progress &&
               settings == o.settings && authoredLevels == o.authoredLevels &&
               savedReplays == o.savedReplays && updatedAt == o.updatedAt;
    }
};

// --- Serialization (versioned, tolerant, forward-compatible) -----------------

namespace detail {

/// base64-encode a free-text token so serialized records stay whitespace-free.
inline std::string encTok(const std::string& s) { return base64Encode(s); }
inline std::string decTok(const std::string& s) {
    std::string out;
    return base64Decode(s, out) ? out : std::string();
}

} // namespace detail

/// Serialize a save to a stable, sorted text form (maps iterate in key order).
inline std::string serialize(const SaveData& s) {
    std::ostringstream os;
    os << "save " << s.version << "\n";
    os << "profile " << detail::encTok(s.profile) << "\n";
    os << "clock " << s.updatedAt << "\n";
    for (const auto& kv : s.settings)
        os << "setting " << detail::encTok(kv.first) << " " << detail::encTok(kv.second) << "\n";
    for (const auto& kv : s.progress)
        os << "progress " << detail::encTok(kv.first) << " " << kv.second.bestStars << " "
           << kv.second.bestTimeMs << "\n";
    for (const std::string& code : s.authoredLevels) os << "level " << code << "\n";
    for (const std::string& code : s.savedReplays) os << "replay " << code << "\n";
    return os.str();
}

/// Migrate a parsed save from @p fromVersion to the current version. Field additions are
/// already handled by tolerant parsing (defaults); this stamps the version and applies any
/// version-specific transform. Kept explicit so future bumps have a home.
inline void migrate(SaveData& s, int fromVersion) {
    // v1 -> v2 added LevelProgressEntry::bestTimeMs (defaulted to 0 by the parser) and the
    // updatedAt clock. Nothing else to transform.
    (void)fromVersion;
    s.version = kSaveVersion;
}

/// Parse a save produced by serialize(). Unknown record types are ignored (forward
/// compatible), a v1 progress line without a time defaults it, and the result is migrated
/// to the current version.
inline SaveData deserialize(const std::string& text) {
    SaveData s;
    int parsedVersion = kSaveVersion;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        std::istringstream ls(line);
        std::string type;
        if (!(ls >> type)) continue;
        if (type == "save") {
            ls >> parsedVersion;
        } else if (type == "profile") {
            std::string tok;
            if (ls >> tok) s.profile = detail::decTok(tok);
        } else if (type == "clock") {
            ls >> s.updatedAt;
        } else if (type == "setting") {
            std::string k, v;
            if (ls >> k) {
                ls >> v; // value token may be empty (encoded empty string)
                s.settings[detail::decTok(k)] = detail::decTok(v);
            }
        } else if (type == "progress") {
            std::string id;
            int stars = 0, timeMs = 0;
            if (ls >> id >> stars) {
                ls >> timeMs; // absent in v1 -> stays 0
                s.progress[detail::decTok(id)] = LevelProgressEntry{stars, timeMs};
            }
        } else if (type == "level") {
            std::string code;
            if (ls >> code) s.authoredLevels.push_back(code);
        } else if (type == "replay") {
            std::string code;
            if (ls >> code) s.savedReplays.push_back(code);
        }
        // else: unknown record -> ignored (a newer save's field on an older build)
    }
    migrate(s, parsedVersion);
    return s;
}

// --- Checksum envelope -------------------------------------------------------

/// Wrap a payload with a checksum header so corruption/truncation is detectable.
inline std::string frameSave(const std::string& payload) {
    return "IKSAVE1 " + std::to_string(detail::fnv1a64(payload)) + "\n" + payload;
}

/// Verify and strip the checksum envelope. Returns false (leaving @p payload empty) on a
/// missing header, a bad/truncated checksum, or a mismatch.
inline bool unframeSave(const std::string& framed, std::string& payload) {
    payload.clear();
    const std::size_t nl = framed.find('\n');
    if (nl == std::string::npos) return false;
    std::istringstream hs(framed.substr(0, nl));
    std::string magic, sum;
    if (!(hs >> magic >> sum) || magic != "IKSAVE1") return false;
    const std::string body = framed.substr(nl + 1);
    std::uint64_t want = 0;
    try {
        want = std::stoull(sum);
    } catch (...) {
        return false;
    }
    if (detail::fnv1a64(body) != want) return false;
    payload = body;
    return true;
}

// --- Storage interface + backends -------------------------------------------

/// A key/value blob store: the seam a real backend (files, cloud) implements.
struct SaveStorage {
    virtual ~SaveStorage() = default;
    virtual bool read(const std::string& key, std::string& out) const = 0;
    virtual bool write(const std::string& key, const std::string& data) = 0;
};

/// In-memory store (headless tests, and a stub cloud backend).
class MemoryStorage : public SaveStorage {
public:
    bool read(const std::string& key, std::string& out) const override {
        auto it = m_data.find(key);
        if (it == m_data.end()) return false;
        out = it->second;
        return true;
    }
    bool write(const std::string& key, const std::string& data) override {
        m_data[key] = data;
        return true;
    }
    bool has(const std::string& key) const { return m_data.count(key) != 0; }
    /// Test helper: truncate a stored blob to simulate corruption.
    void truncate(const std::string& key, std::size_t keep) {
        auto it = m_data.find(key);
        if (it != m_data.end()) it->second = it->second.substr(0, keep);
    }

private:
    std::map<std::string, std::string> m_data;
};

/// File store with an atomic write: serialize to "<path>.tmp" then rename over the target,
/// so a crash mid-write never leaves a half-written save.
class FileStorage : public SaveStorage {
public:
    bool read(const std::string& key, std::string& out) const override {
        std::ifstream in(key, std::ios::binary);
        if (!in) return false;
        std::ostringstream ss;
        ss << in.rdbuf();
        out = ss.str();
        return true;
    }
    bool write(const std::string& key, const std::string& data) override {
        const std::string tmp = key + ".tmp";
        {
            std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
            if (!out) return false;
            out << data;
            if (!out) return false;
        }
        return std::rename(tmp.c_str(), key.c_str()) == 0;
    }
};

// --- Save manager (frame + recover) -----------------------------------------

struct LoadResult {
    bool ok{false};        ///< false only when nothing is stored at @p key.
    bool recovered{false}; ///< true when the stored blob was corrupt and a default was used.
    SaveData data;
};

/// Frames on save; verifies and recovers on load.
class SaveManager {
public:
    explicit SaveManager(SaveStorage& storage) : m_storage(storage) {}

    bool save(const std::string& key, const SaveData& data) {
        return m_storage.write(key, frameSave(serialize(data)));
    }

    LoadResult load(const std::string& key) {
        LoadResult r;
        std::string framed;
        if (!m_storage.read(key, framed)) return r; // nothing stored: ok == false
        std::string payload;
        if (!unframeSave(framed, payload)) {
            r.ok = true;
            r.recovered = true; // corrupt/truncated -> recover to a valid default
            return r;
        }
        r.ok = true;
        r.data = deserialize(payload);
        return r;
    }

private:
    SaveStorage& m_storage;
};

// --- Cloud sync (last-writer-wins) ------------------------------------------

struct SyncOutcome {
    enum Source { Local, Remote, Equal };
    Source source{Equal};
    SaveData merged;
};

/// Reconcile two saves by last-writer-wins on the logical clock. A clock tie is broken
/// deterministically by content hash, so the result is independent of argument order.
inline SyncOutcome syncLastWriterWins(const SaveData& local, const SaveData& remote) {
    SyncOutcome o;
    if (local.updatedAt > remote.updatedAt) {
        o.source = SyncOutcome::Local;
        o.merged = local;
    } else if (remote.updatedAt > local.updatedAt) {
        o.source = SyncOutcome::Remote;
        o.merged = remote;
    } else {
        const std::uint64_t hl = detail::fnv1a64(serialize(local));
        const std::uint64_t hr = detail::fnv1a64(serialize(remote));
        if (hr > hl) {
            o.source = SyncOutcome::Remote;
            o.merged = remote;
        } else {
            o.source = (hr == hl) ? SyncOutcome::Equal : SyncOutcome::Local;
            o.merged = local;
        }
    }
    return o;
}

/// Cloud-sync over any SaveStorage backend (a stub MemoryStorage or a real remote).
class CloudSync {
public:
    explicit CloudSync(SaveStorage& remote) : m_remote(remote) {}

    bool push(const std::string& key, const SaveData& data) {
        return m_remote.write(key, frameSave(serialize(data)));
    }
    /// Pull the remote save; a missing or corrupt blob recovers to a default (clock 0).
    SaveData pull(const std::string& key) {
        std::string framed, payload;
        if (!m_remote.read(key, framed)) return SaveData{};
        if (!unframeSave(framed, payload)) return SaveData{};
        return deserialize(payload);
    }
    /// Pull, last-writer-wins merge with @p local, push the winner back. Returns the outcome.
    SyncOutcome reconcile(const std::string& key, const SaveData& local) {
        const SyncOutcome o = syncLastWriterWins(local, pull(key));
        push(key, o.merged);
        return o;
    }

private:
    SaveStorage& m_remote;
};

// --- Share / import (reuses #317) -------------------------------------------

/// Add an authored level to a save as a #317 share string.
inline void addAuthoredLevel(SaveData& s, const std::string& levelJson) {
    s.authoredLevels.push_back(encodeShare(levelJson));
}
/// Add a saved replay to a save as a #317 share string (the share format is payload-agnostic).
inline void addReplay(SaveData& s, const std::string& replayText) {
    s.savedReplays.push_back(encodeShare(replayText));
}

struct ImportResult {
    bool ok{false};
    std::string payload; ///< the recovered level/replay text.
};

/// Import a shared level or replay code into a save, validating it through the #317 path.
/// A tampered or malformed code yields ok == false and does not modify the save.
inline ImportResult importShared(SaveData& s, const std::string& shareString, bool asReplay = false) {
    ImportResult r;
    const ShareImport imp = decodeShare(shareString);
    if (!imp.ok) return r;
    (asReplay ? s.savedReplays : s.authoredLevels).push_back(shareString);
    r.ok = true;
    r.payload = imp.levelJson;
    return r;
}

} // namespace game
} // namespace IKore
