#pragma once

#include "cv/Rectify.h"        // cv::rectify (paper detect + de-warp + white balance)
#include "game/SymbolSpawns.h" // detectSpawns / loadGame

#include <utility>
#include <vector>

/**
 * @file PhotoLevel.h
 * @brief Photographed drawing -> rectified -> playable spawns (issue #315).
 *
 * A drawing is usually captured as an angled phone photo, not a clean top-down
 * scan. Rectify (#166) already de-warps such a photo (detect the paper quad, solve
 * the homography, warp to a square, white-balance), and SymbolSpawns (#314) turns a
 * clean drawing into game spawns. This wires the two into the single call the mobile
 * capture path wants: photo -> rectify -> recognize -> place, so a photographed
 * sketch is playable end to end.
 *
 * Header-only, dependency-free (std + the header-only CV / game types), so the whole
 * photo-to-playable chain is unit-testable headless and deterministic.
 */
namespace IKore {
namespace game {

struct PhotoLevelOptions {
    int rectifiedSize{256};      ///< side of the square the photo is rectified to.
    SymbolSpawnOptions spawn{};  ///< recognition + placement tunables (worldScale is in rectified px).
};

/// Rectify a photographed drawing to a clean top-down square.
inline cv::Image rectifyPhoto(const cv::Image& photo, int rectifiedSize = 256) {
    return cv::rectify(photo, rectifiedSize);
}

/// Rectify a photographed drawing and recognize its symbols as game spawns.
inline std::vector<EntitySpawn> detectSpawnsFromPhoto(const cv::Image& photo,
                                                      const PhotoLevelOptions& opt = {}) {
    const cv::Image rectified = cv::rectify(photo, opt.rectifiedSize);
    return detectSpawns(rectified, opt.spawn);
}

/// Convenience: a playable DungeonGame straight from a photographed drawing.
inline DungeonGame loadPhotoGame(const cv::Image& photo, const PhotoLevelOptions& opt = {},
                                 std::vector<world::Box> wallBoxes = {}) {
    SceneDescription scene;
    scene.wallBoxes = std::move(wallBoxes);
    scene.spawns = detectSpawnsFromPhoto(photo, opt);
    return loadGame(scene);
}

} // namespace game
} // namespace IKore
