#pragma once

#include <vector>
#include <string>
#include <memory>
#include <unordered_map>
#include <map>
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#include "Texture.h"
#include "Frustum.h"
#include "core/components/AnimationComponent.h"

namespace IKore {

// Forward declarations
class Shader;
class AnimationComponent;

struct Vertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 texCoords;
    glm::vec3 tangent;
    glm::vec3 bitangent;
};

struct Material {
    std::shared_ptr<Texture> diffuse;
    std::shared_ptr<Texture> specular;
    std::shared_ptr<Texture> normal;
    std::shared_ptr<Texture> height;
    std::shared_ptr<Texture> emissive;
    
    // Fallback color values
    glm::vec3 diffuseColor = glm::vec3(1.0f);
    glm::vec3 specularColor = glm::vec3(0.5f);
    glm::vec3 ambientColor = glm::vec3(0.2f);
    float shininess = 32.0f;

    // Opt-in metallic-roughness PBR (issue #234). When isPBR is false (the default)
    // the material uses the Blinn-Phong path unchanged; when true the renderer picks
    // the PBR program and uses the fields below.
    bool isPBR = false;
    glm::vec3 albedo = glm::vec3(1.0f);
    float metallic = 0.0f;
    float roughness = 0.5f;
    float ao = 1.0f;

    // Metallic-roughness PBR textures (issue #269). Optional: when a map is absent the
    // corresponding scalar factor above is used unchanged (glTF factor * texture rule).
    std::shared_ptr<Texture> albedoMap;             // base color
    std::shared_ptr<Texture> metallicRoughnessMap;  // glTF packed: G roughness, B metallic
    std::shared_ptr<Texture> aoMap;                 // ambient occlusion (R)

    // Flags to indicate texture availability
    bool hasDiffuseTexture() const { return diffuse && diffuse->isLoaded(); }
    bool hasSpecularTexture() const { return specular && specular->isLoaded(); }
    bool hasNormalTexture() const { return normal && normal->isLoaded(); }
    bool hasHeightTexture() const { return height && height->isLoaded(); }
    bool hasEmissiveTexture() const { return emissive && emissive->isLoaded(); }
    bool hasAlbedoTexture() const { return albedoMap && albedoMap->isLoaded(); }
    bool hasMetallicRoughnessTexture() const { return metallicRoughnessMap && metallicRoughnessMap->isLoaded(); }
    bool hasAoTexture() const { return aoMap && aoMap->isLoaded(); }
};

class Mesh {
private:
    std::vector<Vertex> m_vertices;
    // Skinned path (issue #260): bone-carrying vertices kept intact instead of
    // being flattened to Vertex, so GPU skinning gets ids/weights.
    std::vector<AnimatedVertex> m_animatedVertices;
    bool m_hasBones = false;
    std::vector<unsigned int> m_indices;
    Material m_material;
    BoundingBox m_boundingBox;

    GLuint m_VAO, m_VBO, m_EBO;
    bool m_isSetup;

    void setupMesh();
    void calculateBoundingBox();

public:
    Mesh(std::vector<Vertex> vertices, std::vector<unsigned int> indices, Material material);
    /// Skinned mesh (issue #260): keeps bone ids/weights and uploads them as
    /// vertex attributes 5 (ivec4) and 6 (vec4) for skinned_phong.vert.
    Mesh(std::vector<AnimatedVertex> animatedVertices, std::vector<unsigned int> indices,
         Material material);
    ~Mesh();
    
    // Delete copy constructor and assignment operator
    Mesh(const Mesh&) = delete;
    Mesh& operator=(const Mesh&) = delete;
    
    // Move constructor and assignment operator
    Mesh(Mesh&& other) noexcept;
    Mesh& operator=(Mesh&& other) noexcept;
    
    void render(std::shared_ptr<Shader> shader) const;
    
    // Getters
    const std::vector<Vertex>& getVertices() const { return m_vertices; }
    const std::vector<unsigned int>& getIndices() const { return m_indices; }
    const Material& getMaterial() const { return m_material; }
    const BoundingBox& getBoundingBox() const { return m_boundingBox; }
    bool isSetup() const { return m_isSetup; }

    // Skinned path (issue #260)
    bool hasBones() const { return m_hasBones; }
    const std::vector<AnimatedVertex>& getAnimatedVertices() const { return m_animatedVertices; }
};

class Model {
private:
    std::vector<std::unique_ptr<Mesh>> m_meshes;
    std::string m_directory;
    std::string m_path;
    BoundingBox m_boundingBox;
    
    // Texture cache to avoid loading duplicate textures
    std::unordered_map<std::string, std::shared_ptr<Texture>> m_textureCache;
    
    // Animation data
    std::map<std::string, BoneInfo> m_boneInfoMap;
    int m_boneCounter = 0;
    bool m_hasAnimations = false;
    std::vector<Animation> m_animations; // animation clips (issue #287)

    // Assimp processing
    void loadModel(const std::string& path);
    void processNode(aiNode* node, const aiScene* scene);
    std::unique_ptr<Mesh> processMesh(aiMesh* mesh, const aiScene* scene);
    Material loadMaterial(aiMaterial* mat);
    std::shared_ptr<Texture> loadMaterialTexture(aiMaterial* mat, aiTextureType type, 
                                                Texture::Type textureType);
    
    // Animation processing
    void extractBoneWeightForVertices(std::vector<AnimatedVertex>& vertices, aiMesh* mesh, const aiScene* scene);
    void setBoneIdAndWeights(AnimatedVertex& vertex, int boneID, float weight);
    // Read the scene's animation channels into Animation clips (issue #287). Keyframe
    // timestamps are converted from ticks to seconds so the runtime advances them by dt.
    void extractAnimations(const aiScene* scene);
    
    // Helper functions
    std::string getTextureFilename(aiMaterial* mat, aiTextureType type, unsigned int index = 0);
    Texture::Type aiTextureTypeToTextureType(aiTextureType type);
    void calculateModelBoundingBox();

public:
    Model() = default;
    ~Model() = default;
    
    // Delete copy constructor and assignment operator
    Model(const Model&) = delete;
    Model& operator=(const Model&) = delete;
    
    // Move constructor and assignment operator
    Model(Model&& other) noexcept;
    Model& operator=(Model&& other) noexcept;
    
    // Load model from file
    bool loadFromFile(const std::string& path);
    
    // Render the entire model
    void render(std::shared_ptr<Shader> shader) const;

    // Render each mesh with the program its material selects (issue #269): meshes with
    // Material::isPBR draw with @p pbrShader, the rest with @p phongShader. Per-mesh
    // model/normal matrices are set on the chosen program; frame/light uniforms must
    // already be set on both. If @p pbrShader is null, everything uses @p phongShader.
    void renderSelectable(std::shared_ptr<Shader> phongShader, std::shared_ptr<Shader> pbrShader,
                          const glm::mat4& model, const glm::mat3& normalMatrix) const;
    
    // Getters
    const std::vector<std::unique_ptr<Mesh>>& getMeshes() const { return m_meshes; }
    const std::string& getPath() const { return m_path; }
    const std::string& getDirectory() const { return m_directory; }
    const BoundingBox& getBoundingBox() const { return m_boundingBox; }
    size_t getMeshCount() const { return m_meshes.size(); }
    bool isEmpty() const { return m_meshes.empty(); }
    
    // Animation getters
    bool hasAnimations() const { return m_hasAnimations; }
    const std::map<std::string, BoneInfo>& getBoneInfoMap() const { return m_boneInfoMap; }
    int getBoneCount() const { return m_boneCounter; }
    // Animation clips extracted from the model, ready to drive an AnimationComponent (#287).
    const std::vector<Animation>& getAnimations() const { return m_animations; }

    // True if any mesh carries bone weights, i.e. the model needs the skinned vertex
    // path (used to pick the skinned shadow-depth program, issue #266).
    bool hasBones() const {
        for (const auto& mesh : m_meshes) {
            if (mesh && mesh->hasBones()) return true;
        }
        return false;
    }
    
    // Static utility functions
    static std::shared_ptr<Model> loadFromFileShared(const std::string& path);
    static std::string getSupportedFormats();
};

} // namespace IKore
