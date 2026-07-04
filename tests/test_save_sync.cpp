// Save, cloud sync, and share plumbing - issue #370.
//
// Verifies the unified save layer behind a storage interface: a save round-trips (including
// free text with spaces), a v1 blob migrates across a version bump, a truncated blob
// recovers to a valid default, last-writer-wins reconciles a divergent local/remote pair
// deterministically, and a shared level/replay code imports through the #317 path. Pure std
// + header-only:
//   g++ -std=c++17 -I src tests/test_save_sync.cpp -o test_save_sync

#include "game/SaveSync.h"

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

static SaveData sampleSave() {
    SaveData s;
    s.profile = "Ada Lovelace"; // free text with a space
    s.updatedAt = 42;
    s.progress["world1/level3"] = LevelProgressEntry{3, 15200};
    s.progress["world1/level1"] = LevelProgressEntry{2, 9800};
    s.settings["audio.master"] = "0.8";
    s.settings["ui.language"] = "en GB"; // value with a space
    addAuthoredLevel(s, "{\"name\":\"my level\"}");
    addReplay(s, "replay:frames=1234;seed=7");
    return s;
}

int main() {
    // 1. Round-trip at the current version through the manager + a memory store.
    {
        MemoryStorage store;
        SaveManager mgr(store);
        const SaveData in = sampleSave();
        CHECK(mgr.save("slot0", in));
        const LoadResult out = mgr.load("slot0");
        CHECK(out.ok && !out.recovered);
        CHECK(out.data == in); // exact, including spaced strings and share codes
    }

    // 2. A v1 save (no per-level time, no clock) migrates across the version bump.
    {
        // Hand-author a v1 payload: progress lines carry only stars.
        std::ostringstream v1;
        v1 << "save 1\n";
        v1 << "profile " << detail::encTok("Grace") << "\n";
        v1 << "progress " << detail::encTok("w1/l1") << " 3\n";
        v1 << "setting " << detail::encTok("audio.master") << " " << detail::encTok("1.0") << "\n";
        v1 << "future_record ignored payload\n"; // forward-compat: unknown record skipped
        const std::string framed = frameSave(v1.str());

        MemoryStorage store;
        store.write("slot0", framed);
        SaveManager mgr(store);
        const LoadResult out = mgr.load("slot0");
        CHECK(out.ok && !out.recovered);
        CHECK(out.data.version == kSaveVersion); // migrated to current
        CHECK(out.data.profile == "Grace");
        CHECK(out.data.progress.at("w1/l1").bestStars == 3);
        CHECK(out.data.progress.at("w1/l1").bestTimeMs == 0); // defaulted by migration
        CHECK(out.data.settings.at("audio.master") == "1.0");
        // Re-saving yields a current-version blob that round-trips.
        CHECK(mgr.save("slot1", out.data));
        CHECK(mgr.load("slot1").data == out.data);
    }

    // 3. A truncated/corrupt blob is detected and recovers to a valid default.
    {
        MemoryStorage store;
        SaveManager mgr(store);
        CHECK(mgr.save("slot0", sampleSave()));
        std::string blob;
        store.read("slot0", blob);
        store.truncate("slot0", blob.size() / 2); // corrupt the payload
        const LoadResult out = mgr.load("slot0");
        CHECK(out.ok && out.recovered);            // corruption detected, not loaded as garbage
        CHECK(out.data == SaveData{});             // recovered to a valid empty save
        CHECK(out.data.version == kSaveVersion);
        // A missing key is a clean miss (not a recovery).
        const LoadResult missing = mgr.load("does_not_exist");
        CHECK(!missing.ok && !missing.recovered);
    }

    // 4. Last-writer-wins reconciles deterministically, regardless of argument order.
    {
        SaveData local = sampleSave();
        local.updatedAt = 100;
        SaveData remote = sampleSave();
        remote.updatedAt = 50;
        remote.profile = "Older";

        CHECK(syncLastWriterWins(local, remote).source == SyncOutcome::Local);
        CHECK(syncLastWriterWins(local, remote).merged == local);
        // The newer save wins regardless of argument order (the source label is arg-relative).
        CHECK(syncLastWriterWins(remote, local).merged == local);

        // A clock tie is broken deterministically: same winner whichever order.
        SaveData a = sampleSave();
        SaveData b = sampleSave();
        a.updatedAt = b.updatedAt = 7;
        b.profile = "Different"; // same clock, different content
        const SyncOutcome ab = syncLastWriterWins(a, b);
        const SyncOutcome ba = syncLastWriterWins(b, a);
        CHECK(ab.merged == ba.merged); // order-independent
    }

    // 5. CloudSync over a stub backend: pull, merge, push the winner back.
    {
        MemoryStorage cloud;
        CloudSync sync(cloud);
        SaveData remote = sampleSave();
        remote.updatedAt = 3;
        remote.profile = "Remote";
        sync.push("slot0", remote);

        SaveData local = sampleSave();
        local.updatedAt = 9; // newer
        local.profile = "Local";
        const SyncOutcome out = sync.reconcile("slot0", local);
        CHECK(out.source == SyncOutcome::Local);
        CHECK(out.merged.profile == "Local");
        // The winner was pushed back to the cloud.
        CHECK(sync.pull("slot0").profile == "Local");
    }

    // 6. Share/import round-trips a level and a replay through the #317 code path.
    {
        const std::string levelJson = "{\"walls\":[[0,0],[1,0]],\"name\":\"shared\"}";
        SaveData author;
        addAuthoredLevel(author, levelJson);

        SaveData importer;
        const ImportResult ir = importShared(importer, author.authoredLevels[0]);
        CHECK(ir.ok && ir.payload == levelJson);
        CHECK(importer.authoredLevels.size() == 1);

        // A tampered/malformed code is rejected and does not modify the save.
        const ImportResult bad = importShared(importer, "DDL1:DD-BOGUS:@@notbase64@@");
        CHECK(!bad.ok);
        CHECK(importer.authoredLevels.size() == 1);

        // Replays share through the same path.
        const std::string replay = "replay:seed=99;len=4321";
        SaveData rAuthor;
        addReplay(rAuthor, replay);
        const ImportResult rr = importShared(importer, rAuthor.savedReplays[0], /*asReplay=*/true);
        CHECK(rr.ok && rr.payload == replay);
        CHECK(importer.savedReplays.size() == 1);
    }

    // 7. FileStorage writes atomically (temp-then-rename) and reads back intact.
    {
        const std::string path = "test_save_sync_scratch.iksave";
        FileStorage fs;
        SaveManager mgr(fs);
        const SaveData in = sampleSave();
        if (mgr.save(path, in)) { // skip silently if the CWD is not writable
            const LoadResult out = mgr.load(path);
            CHECK(out.ok && !out.recovered);
            CHECK(out.data == in);
            std::remove(path.c_str());
            std::remove((path + ".tmp").c_str());
        }
    }

    if (g_failures == 0) {
        std::printf("test_save_sync: all checks passed\n");
        return 0;
    }
    std::printf("test_save_sync: %d check(s) failed\n", g_failures);
    return 1;
}
