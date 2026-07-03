#pragma once

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <vector>

/**
 * @file CascadedShadowMap.h
 * @brief GPU-side cascaded shadow map for the directional light (issue #265).
 *
 * Wraps a GL_TEXTURE_2D_ARRAY depth texture (one layer per cascade), a single
 * framebuffer whose depth attachment is re-pointed at each layer, and the per-frame
 * light matrices produced by render::buildCascades (the tested, glm-free split/fit
 * core in src/render/). The renderer runs one depth pass per cascade and then binds
 * the whole array plus the light matrices and split depths for the lighting shader,
 * which selects a cascade per fragment mirroring render::selectCascade.
 *
 * With cascadeCount == 1 this is a single tightly fitted directional map - the same
 * shape the pre-CSM ShadowMap produced - so the single-cascade path stays available.
 * All CPU split/fit math lives in the tested core; this class only owns GL objects
 * and forwards results.
 */
namespace IKore {

class CascadedShadowMap {
public:
    static constexpr int kMaxCascades = 4;

    explicit CascadedShadowMap(int cascadeCount = 4, int resolution = 2048);
    ~CascadedShadowMap();

    CascadedShadowMap(const CascadedShadowMap&) = delete;
    CascadedShadowMap& operator=(const CascadedShadowMap&) = delete;

    /// Allocate the depth texture array and framebuffer. Idempotent.
    bool initialize();
    bool isInitialized() const { return m_initialized; }

    /**
     * @brief Recompute the per-cascade light matrices and split depths for this frame.
     * @param cameraView  Camera view matrix (world -> view).
     * @param fovYRadians Camera vertical field of view, radians.
     * @param aspect      Camera aspect ratio (width / height).
     * @param nearZ,farZ  Camera near/far plane distances (view space).
     * @param lightDir    Direction the directional light travels.
     * @param splitLambda Split blend (0 uniform .. 1 logarithmic).
     *
     * Delegates entirely to render::buildCascades; no fitting math lives here.
     */
    void computeCascades(const glm::mat4& cameraView, float fovYRadians, float aspect,
                         float nearZ, float farZ, const glm::vec3& lightDir, float splitLambda);

    /// Bind the framebuffer and target cascade layer @p index for its depth pass.
    void beginCascadePass(int index);
    /// Restore the previous framebuffer/viewport after the last cascade pass.
    void endShadowPass();

    /// Bind the depth texture array for sampling in the lighting shader.
    void bindArray(int textureUnit) const;

    int cascadeCount() const { return m_cascadeCount; }
    int resolution() const { return m_resolution; }
    GLuint textureArray() const { return m_depthArray; }

    /// The light matrix for each cascade (size == cascadeCount), for shader upload.
    const std::vector<glm::mat4>& lightMatrices() const { return m_lightMatrices; }
    /// The far view-depth boundary of each cascade (size == cascadeCount).
    const std::vector<float>& splitDepths() const { return m_splitDepths; }

private:
    void cleanup();

    int m_cascadeCount;
    int m_resolution;
    bool m_initialized{false};

    GLuint m_framebuffer{0};
    GLuint m_depthArray{0};

    std::vector<glm::mat4> m_lightMatrices;
    std::vector<float> m_splitDepths;

    GLint m_previousViewport[4]{0, 0, 0, 0};
};

} // namespace IKore
