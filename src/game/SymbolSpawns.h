#pragma once

#include "cv/Symbols.h"       // cv::Image, cv::SymbolInstance, detectSymbols
#include "game/DungeonGame.h" // SceneDescription, EntitySpawn, DungeonGame, loadGame

#include <string>
#include <vector>

/**
 * @file SymbolSpawns.h
 * @brief Recognize hand-drawn symbols and place them as game spawns (issue #314).
 *
 * The Tier-1 recognizer (#169) already classifies a colored blob into the game
 * vocabulary ("player"/"enemy"/"coin"/"exit"), and LevelReview (#170) can fold CV
 * outputs into an editable LevelSpec. This closes the remaining gap: a single call
 * that turns a (rectified) drawing image straight into the DungeonGame spawn list,
 * so a photographed/drawn sketch is playable without the intermediate review model.
 *
 * The recognizer's object strings are exactly loadGame()'s spawn vocabulary, so:
 *   Image -> detectSymbols -> placeSpawns -> SceneDescription -> loadGame
 * is a complete drawing-to-playable path. Symbol centroids (image pixels, y down)
 * are scaled into world XZ by @c worldScale, matching assembleLevel()'s convention
 * so symbol placement agrees with the wall placement in the same pipeline.
 *
 * Header-only, dependency-free (std + the header-only CV / game types), so the whole
 * chain is unit-testable headless and deterministic.
 */
namespace IKore {
namespace game {

struct SymbolSpawnOptions {
    float worldScale{1.0f};         ///< image pixels -> world units (matches assembleLevel).
    cv::SymbolOptions detect{};     ///< blob detection + recognition tunables.
    bool dropUnknown{false};        ///< skip blobs the recognizer could not name.
    bool extendedVocabulary{false}; ///< use color+shape to recognize keys/doors/switches/
                                    ///< hazards and enemy behaviors (#336); off keeps the
                                    ///< Tier-1 color-only mapping.
};

/// Map already-detected symbol instances (image-space centroids) to entity spawns. With
/// extendedVocabulary, a symbol's type comes from its color AND shape (#336) so the richer
/// drawable types are recognized; otherwise the Tier-1 color mapping is used.
inline std::vector<EntitySpawn> placeSpawns(const std::vector<cv::SymbolInstance>& symbols,
                                            const SymbolSpawnOptions& opt = {}) {
    std::vector<EntitySpawn> spawns;
    spawns.reserve(symbols.size());
    for (const cv::SymbolInstance& s : symbols) {
        if (opt.dropUnknown && s.result.color == cv::ColorClass::Unknown) continue;
        const std::string type = opt.extendedVocabulary
                                     ? cv::extendedObject(s.result.color, s.result.shape)
                                     : s.result.object;
        if (type.empty()) continue;
        spawns.push_back(
            EntitySpawn{type, ecs::Vec3{s.cx * opt.worldScale, 0.0f, s.cy * opt.worldScale}, 0.0f});
    }
    return spawns;
}

/// Detect + recognize + place every symbol in @p img in one step.
inline std::vector<EntitySpawn> detectSpawns(const cv::Image& img,
                                             const SymbolSpawnOptions& opt = {}) {
    return placeSpawns(cv::detectSymbols(img, opt.detect), opt);
}

/**
 * @brief Build a SceneDescription from recognized symbols (optionally over existing
 *        wall geometry). The spawns come straight from the drawing's symbols.
 */
inline SceneDescription symbolsToScene(const cv::Image& img, const SymbolSpawnOptions& opt = {},
                                       std::vector<world::Box> wallBoxes = {}) {
    SceneDescription scene;
    scene.wallBoxes = std::move(wallBoxes);
    scene.spawns = detectSpawns(img, opt);
    return scene;
}

/// Convenience: a playable DungeonGame straight from a recognized drawing.
inline DungeonGame loadSymbolGame(const cv::Image& img, const SymbolSpawnOptions& opt = {},
                                  std::vector<world::Box> wallBoxes = {}) {
    return loadGame(symbolsToScene(img, opt, std::move(wallBoxes)));
}

} // namespace game
} // namespace IKore
