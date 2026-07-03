#include "Model.h"
#include "Shader.h"
#include "core/Logger.h"
#include <glm/gtc/type_ptr.hpp>    // glm::value_ptr for renderSelectable (issue #269)
#include <glm/gtc/quaternion.hpp>  // glm::quat for animation keyframes (issue #287)

#include <iostream>
#include <filesystem>

namespace IKore {

// Mesh Implementation
Mesh::Mesh(std::vector<Vertex> vertices, std::vector<unsigned int> indices, Material material)
    : m_vertices(std::move(vertices)), m_indices(std::move(indices)), m_material(std::move(material)),
      m_VAO(0), m_VBO(0), m_EBO(0), m_isSetup(false) {
    calculateBoundingBox();
    setupMesh();
}

Mesh::Mesh(std::vector<AnimatedVertex> animatedVertices, std::vector<unsigned int> indices,
           Material material)
    : m_animatedVertices(std::move(animatedVertices)), m_hasBones(true),
      m_indices(std::move(indices)), m_material(std::move(material)),
      m_VAO(0), m_VBO(0), m_EBO(0), m_isSetup(false) {
    calculateBoundingBox();
    setupMesh();
}

Mesh::~Mesh() {
    // Check if OpenGL functions are available before cleanup
    if (m_VAO && glDeleteVertexArrays) glDeleteVertexArrays(1, &m_VAO);
    if (m_VBO && glDeleteBuffers) glDeleteBuffers(1, &m_VBO);
    if (m_EBO && glDeleteBuffers) glDeleteBuffers(1, &m_EBO);
}

Mesh::Mesh(Mesh&& other) noexcept
    : m_vertices(std::move(other.m_vertices)),
      m_animatedVertices(std::move(other.m_animatedVertices)), m_hasBones(other.m_hasBones),
      m_indices(std::move(other.m_indices)),
      m_material(std::move(other.m_material)), m_boundingBox(other.m_boundingBox),
      m_VAO(other.m_VAO), m_VBO(other.m_VBO), m_EBO(other.m_EBO), m_isSetup(other.m_isSetup) {
    other.m_VAO = other.m_VBO = other.m_EBO = 0;
    other.m_isSetup = false;
    other.m_hasBones = false;
}

Mesh& Mesh::operator=(Mesh&& other) noexcept {
    if (this != &other) {
        // Clean up current resources
        if (m_VAO) glDeleteVertexArrays(1, &m_VAO);
        if (m_VBO) glDeleteBuffers(1, &m_VBO);
        if (m_EBO) glDeleteBuffers(1, &m_EBO);
        
        // Move data
        m_vertices = std::move(other.m_vertices);
        m_animatedVertices = std::move(other.m_animatedVertices);
        m_hasBones = other.m_hasBones;
        m_indices = std::move(other.m_indices);
        m_material = std::move(other.m_material);
        m_boundingBox = other.m_boundingBox;
        m_VAO = other.m_VAO;
        m_VBO = other.m_VBO;
        m_EBO = other.m_EBO;
        m_isSetup = other.m_isSetup;

        // Reset other object
        other.m_VAO = other.m_VBO = other.m_EBO = 0;
        other.m_isSetup = false;
        other.m_hasBones = false;
    }
    return *this;
}

void Mesh::setupMesh() {
    // Check if OpenGL functions are available (for headless testing environments)
    if (!glGenVertexArrays) {
        LOG_WARNING("OpenGL not available - mesh setup skipped (headless mode)");
        m_isSetup = false;
        return;
    }
    
    glGenVertexArrays(1, &m_VAO);
    glGenBuffers(1, &m_VBO);
    glGenBuffers(1, &m_EBO);
    
    glBindVertexArray(m_VAO);

    // Load index data
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, m_indices.size() * sizeof(unsigned int), &m_indices[0], GL_STATIC_DRAW);

    glBindBuffer(GL_ARRAY_BUFFER, m_VBO);
    if (m_hasBones) {
        // Skinned layout (issue #260): AnimatedVertex keeps the same attributes 0-4
        // as Vertex plus bone ids (5, integer attribute) and weights (6) for
        // skinned_phong.vert's linear-blend skinning.
        glBufferData(GL_ARRAY_BUFFER, m_animatedVertices.size() * sizeof(AnimatedVertex),
                     &m_animatedVertices[0], GL_STATIC_DRAW);

        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(AnimatedVertex),
                              (void*)offsetof(AnimatedVertex, position));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(AnimatedVertex),
                              (void*)offsetof(AnimatedVertex, normal));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(AnimatedVertex),
                              (void*)offsetof(AnimatedVertex, texCoords));
        glEnableVertexAttribArray(3);
        glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, sizeof(AnimatedVertex),
                              (void*)offsetof(AnimatedVertex, tangent));
        glEnableVertexAttribArray(4);
        glVertexAttribPointer(4, 3, GL_FLOAT, GL_FALSE, sizeof(AnimatedVertex),
                              (void*)offsetof(AnimatedVertex, bitangent));
        glEnableVertexAttribArray(5);
        glVertexAttribIPointer(5, MAX_BONE_INFLUENCE, GL_INT, sizeof(AnimatedVertex),
                               (void*)offsetof(AnimatedVertex, boneIDs));
        glEnableVertexAttribArray(6);
        glVertexAttribPointer(6, MAX_BONE_INFLUENCE, GL_FLOAT, GL_FALSE, sizeof(AnimatedVertex),
                              (void*)offsetof(AnimatedVertex, weights));
    } else {
        glBufferData(GL_ARRAY_BUFFER, m_vertices.size() * sizeof(Vertex), &m_vertices[0], GL_STATIC_DRAW);

        // Vertex positions
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)0);

        // Vertex normals
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, normal));

        // Vertex texture coordinates
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, texCoords));

        // Vertex tangent
        glEnableVertexAttribArray(3);
        glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, tangent));

        // Vertex bitangent
        glEnableVertexAttribArray(4);
        glVertexAttribPointer(4, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, bitangent));
    }

    glBindVertexArray(0);
    m_isSetup = true;
}

void Mesh::calculateBoundingBox() {
    if (m_hasBones) {
        if (m_animatedVertices.empty()) {
            m_boundingBox = BoundingBox();
            return;
        }
        glm::vec3 minPoint = m_animatedVertices[0].position;
        glm::vec3 maxPoint = m_animatedVertices[0].position;
        for (const auto& vertex : m_animatedVertices) {
            minPoint = glm::min(minPoint, vertex.position);
            maxPoint = glm::max(maxPoint, vertex.position);
        }
        m_boundingBox = BoundingBox(minPoint, maxPoint);
        return;
    }

    if (m_vertices.empty()) {
        m_boundingBox = BoundingBox();
        return;
    }

    // Initialize with first vertex
    glm::vec3 minPoint = m_vertices[0].position;
    glm::vec3 maxPoint = m_vertices[0].position;

    // Find min and max coordinates
    for (const auto& vertex : m_vertices) {
        minPoint = glm::min(minPoint, vertex.position);
        maxPoint = glm::max(maxPoint, vertex.position);
    }

    m_boundingBox = BoundingBox(minPoint, maxPoint);
}

void Mesh::render(std::shared_ptr<Shader> shader) const {
    if (!m_isSetup || !shader) return;
    
    shader->use();

    // Bind textures and set uniforms
    GLuint textureUnit = 0;

    if (m_material.isPBR) {
        // Metallic-roughness PBR binding (issue #269): scalar factors plus optional
        // albedo / metallic-roughness / AO / normal maps. The shader multiplies each
        // factor by its map (or 1 when absent), mirroring render::resolvePbrInputs.
        shader->setVec3("material.albedo", m_material.albedo.x, m_material.albedo.y, m_material.albedo.z);
        shader->setFloat("material.metallic", m_material.metallic);
        shader->setFloat("material.roughness", m_material.roughness);
        shader->setFloat("material.ao", m_material.ao);

        if (m_material.hasAlbedoTexture()) {
            m_material.albedoMap->bind(textureUnit);
            shader->setInt("useAlbedoMap", 1);
            glUniform1i(glGetUniformLocation(shader->id(), "albedoMap"), textureUnit++);
        } else {
            shader->setInt("useAlbedoMap", 0);
        }
        if (m_material.hasMetallicRoughnessTexture()) {
            m_material.metallicRoughnessMap->bind(textureUnit);
            shader->setInt("useMetallicRoughnessMap", 1);
            glUniform1i(glGetUniformLocation(shader->id(), "metallicRoughnessMap"), textureUnit++);
        } else {
            shader->setInt("useMetallicRoughnessMap", 0);
        }
        if (m_material.hasAoTexture()) {
            m_material.aoMap->bind(textureUnit);
            shader->setInt("useAoMap", 1);
            glUniform1i(glGetUniformLocation(shader->id(), "aoMap"), textureUnit++);
        } else {
            shader->setInt("useAoMap", 0);
        }
        if (m_material.hasNormalTexture()) {
            m_material.normal->bind(textureUnit);
            shader->setInt("useNormalMap", 1);
            glUniform1i(glGetUniformLocation(shader->id(), "normalMap"), textureUnit++);
        } else {
            shader->setInt("useNormalMap", 0);
        }
    } else {
        // Blinn-Phong binding (unchanged).
        // Diffuse texture
        if (m_material.hasDiffuseTexture()) {
            m_material.diffuse->bind(textureUnit);
            shader->setFloat("material.useDiffuseTexture", 1.0f);
            glUniform1i(glGetUniformLocation(shader->id(), "material.diffuse"), textureUnit++);
        } else {
            shader->setFloat("material.useDiffuseTexture", 0.0f);
        }

        // Specular texture
        if (m_material.hasSpecularTexture()) {
            m_material.specular->bind(textureUnit);
            shader->setFloat("material.useSpecularTexture", 1.0f);
            glUniform1i(glGetUniformLocation(shader->id(), "material.specular"), textureUnit++);
        } else {
            shader->setFloat("material.useSpecularTexture", 0.0f);
        }

        // Normal texture
        if (m_material.hasNormalTexture()) {
            m_material.normal->bind(textureUnit);
            shader->setFloat("material.useNormalTexture", 1.0f);
            glUniform1i(glGetUniformLocation(shader->id(), "material.normal"), textureUnit++);
        } else {
            shader->setFloat("material.useNormalTexture", 0.0f);
        }

        // Set material color properties
        shader->setVec3("material.ambientColor", m_material.ambientColor.x, m_material.ambientColor.y, m_material.ambientColor.z);
        shader->setVec3("material.diffuseColor", m_material.diffuseColor.x, m_material.diffuseColor.y, m_material.diffuseColor.z);
        shader->setVec3("material.specularColor", m_material.specularColor.x, m_material.specularColor.y, m_material.specularColor.z);
        shader->setFloat("material.shininess", m_material.shininess);
    }

    // Draw mesh
    glBindVertexArray(m_VAO);
    glDrawElements(GL_TRIANGLES, static_cast<unsigned int>(m_indices.size()), GL_UNSIGNED_INT, 0);
    glBindVertexArray(0);
    
    // Unbind textures
    glActiveTexture(GL_TEXTURE0);
}

// Model Implementation
Model::Model(Model&& other) noexcept 
    : m_meshes(std::move(other.m_meshes)), m_directory(std::move(other.m_directory)),
      m_path(std::move(other.m_path)), m_boundingBox(other.m_boundingBox),
      m_textureCache(std::move(other.m_textureCache)) {
}

Model& Model::operator=(Model&& other) noexcept {
    if (this != &other) {
        m_meshes = std::move(other.m_meshes);
        m_directory = std::move(other.m_directory);
        m_path = std::move(other.m_path);
        m_boundingBox = other.m_boundingBox;
        m_textureCache = std::move(other.m_textureCache);
    }
    return *this;
}

bool Model::loadFromFile(const std::string& path) {
    LOG_INFO("Loading model from: " + path);
    
    if (!std::filesystem::exists(path)) {
        LOG_ERROR("Model file does not exist: " + path);
        return false;
    }
    
    m_path = path;
    m_directory = path.substr(0, path.find_last_of('/'));
    
    try {
        loadModel(path);
        calculateModelBoundingBox();
        LOG_INFO("Successfully loaded model: " + path + " with " + std::to_string(m_meshes.size()) + " meshes");
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to load model: " + path + " - " + std::string(e.what()));
        return false;
    }
}

void Model::loadModel(const std::string& path) {
    Assimp::Importer importer;
    
    // Configure Assimp post-processing flags
    unsigned int flags = aiProcess_Triangulate |
                        aiProcess_FlipUVs |
                        aiProcess_CalcTangentSpace |
                        aiProcess_GenSmoothNormals |
                        aiProcess_JoinIdenticalVertices |
                        aiProcess_ImproveCacheLocality |
                        aiProcess_OptimizeMeshes |
                        aiProcess_OptimizeGraph;
    
    const aiScene* scene = importer.ReadFile(path, flags);
    
    if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode) {
        throw std::runtime_error("Assimp error: " + std::string(importer.GetErrorString()));
    }
    
    // Check if the model has animations
    m_hasAnimations = scene->mNumAnimations > 0;
    if (m_hasAnimations) {
        LOG_INFO("Model has " + std::to_string(scene->mNumAnimations) + " animations");
    }

    processNode(scene->mRootNode, scene);

    // Extract the animation clips after the meshes, so m_boneInfoMap (bone ids + offsets,
    // filled by processMesh) is available to key each animated bone (issue #287).
    if (m_hasAnimations) {
        extractAnimations(scene);
    }
}

void Model::extractAnimations(const aiScene* scene) {
    m_animations.clear();
    for (unsigned int a = 0; a < scene->mNumAnimations; ++a) {
        const aiAnimation* aiAnim = scene->mAnimations[a];
        // Assimp reports keyframe times in ticks; convert to seconds so the runtime can
        // advance the animation by a per-frame dt in seconds.
        const double tps = aiAnim->mTicksPerSecond != 0.0 ? aiAnim->mTicksPerSecond : 25.0;

        Animation anim;
        anim.name = aiAnim->mName.length > 0 ? std::string(aiAnim->mName.C_Str())
                                             : ("animation_" + std::to_string(a));
        anim.ticksPerSecond = 1.0f; // timestamps below are already in seconds
        anim.duration = static_cast<float>(aiAnim->mDuration / tps);

        for (unsigned int c = 0; c < aiAnim->mNumChannels; ++c) {
            const aiNodeAnim* ch = aiAnim->mChannels[c];
            Bone bone;
            bone.name = ch->mNodeName.C_Str();
            auto it = m_boneInfoMap.find(bone.name);
            bone.id = (it != m_boneInfoMap.end()) ? it->second.id : -1;

            for (unsigned int k = 0; k < ch->mNumPositionKeys; ++k) {
                const aiVectorKey& key = ch->mPositionKeys[k];
                bone.positions.push_back({glm::vec3(key.mValue.x, key.mValue.y, key.mValue.z),
                                          static_cast<float>(key.mTime / tps)});
            }
            for (unsigned int k = 0; k < ch->mNumRotationKeys; ++k) {
                const aiQuatKey& key = ch->mRotationKeys[k];
                bone.rotations.push_back({glm::quat(key.mValue.w, key.mValue.x, key.mValue.y, key.mValue.z),
                                          static_cast<float>(key.mTime / tps)});
            }
            for (unsigned int k = 0; k < ch->mNumScalingKeys; ++k) {
                const aiVectorKey& key = ch->mScalingKeys[k];
                bone.scales.push_back({glm::vec3(key.mValue.x, key.mValue.y, key.mValue.z),
                                       static_cast<float>(key.mTime / tps)});
            }
            anim.bones.push_back(std::move(bone));
        }
        anim.boneInfoMap = m_boneInfoMap;
        m_animations.push_back(std::move(anim));
    }
}

void Model::processNode(aiNode* node, const aiScene* scene) {
    // Process all meshes in the current node
    for (unsigned int i = 0; i < node->mNumMeshes; i++) {
        aiMesh* mesh = scene->mMeshes[node->mMeshes[i]];
        m_meshes.push_back(processMesh(mesh, scene));
    }
    
    // Process all child nodes
    for (unsigned int i = 0; i < node->mNumChildren; i++) {
        processNode(node->mChildren[i], scene);
    }
}

std::unique_ptr<Mesh> Model::processMesh(aiMesh* mesh, const aiScene* scene) {
    std::vector<Vertex> vertices;
    std::vector<unsigned int> indices;
    
    // Check if mesh has bones for skeletal animation
    bool hasBones = mesh->mNumBones > 0;
    std::vector<AnimatedVertex> animatedVertices;
    
    if (hasBones) {
        // Process animated vertices
        for (unsigned int i = 0; i < mesh->mNumVertices; i++) {
            AnimatedVertex vertex;
            
            // Positions
            vertex.position.x = mesh->mVertices[i].x;
            vertex.position.y = mesh->mVertices[i].y;
            vertex.position.z = mesh->mVertices[i].z;
            
            // Normals
            if (mesh->HasNormals()) {
                vertex.normal.x = mesh->mNormals[i].x;
                vertex.normal.y = mesh->mNormals[i].y;
                vertex.normal.z = mesh->mNormals[i].z;
            } else {
                vertex.normal = glm::vec3(0.0f, 1.0f, 0.0f);
            }
            
            // Texture coordinates
            if (mesh->mTextureCoords[0]) {
                vertex.texCoords.x = mesh->mTextureCoords[0][i].x;
                vertex.texCoords.y = mesh->mTextureCoords[0][i].y;
            } else {
                vertex.texCoords = glm::vec2(0.0f, 0.0f);
            }
            
            // Tangents
            if (mesh->HasTangentsAndBitangents()) {
                vertex.tangent.x = mesh->mTangents[i].x;
                vertex.tangent.y = mesh->mTangents[i].y;
                vertex.tangent.z = mesh->mTangents[i].z;
                
                vertex.bitangent.x = mesh->mBitangents[i].x;
                vertex.bitangent.y = mesh->mBitangents[i].y;
                vertex.bitangent.z = mesh->mBitangents[i].z;
            } else {
                vertex.tangent = glm::vec3(1.0f, 0.0f, 0.0f);
                vertex.bitangent = glm::vec3(0.0f, 0.0f, 1.0f);
            }
            
            animatedVertices.push_back(vertex);
        }
        
        // Extract bone data
        extractBoneWeightForVertices(animatedVertices, mesh, scene);

        // Keep the bone-carrying vertices intact for GPU skinning (issue #260) and
        // normalize each vertex's influence weights to sum to 1, mirroring the
        // unit-tested render::skin::normalizeInfluenceWeights.
        for (auto& animVertex : animatedVertices) {
            float sum = 0.0f;
            for (int w = 0; w < MAX_BONE_INFLUENCE; ++w) {
                if (animVertex.weights[w] > 0.0f) sum += animVertex.weights[w];
                else animVertex.weights[w] = 0.0f;
            }
            if (sum <= 0.0f) {
                animVertex.weights[0] = 1.0f;
            } else {
                for (int w = 0; w < MAX_BONE_INFLUENCE; ++w) animVertex.weights[w] /= sum;
            }
        }
    } else {
        // Process regular vertices
        for (unsigned int i = 0; i < mesh->mNumVertices; i++) {
            Vertex vertex;
            
            // Positions
            vertex.position.x = mesh->mVertices[i].x;
            vertex.position.y = mesh->mVertices[i].y;
            vertex.position.z = mesh->mVertices[i].z;
            
            // Normals
            if (mesh->HasNormals()) {
                vertex.normal.x = mesh->mNormals[i].x;
                vertex.normal.y = mesh->mNormals[i].y;
                vertex.normal.z = mesh->mNormals[i].z;
            } else {
                vertex.normal = glm::vec3(0.0f, 1.0f, 0.0f);
            }
            
            // Texture coordinates
            if (mesh->mTextureCoords[0]) {
                vertex.texCoords.x = mesh->mTextureCoords[0][i].x;
                vertex.texCoords.y = mesh->mTextureCoords[0][i].y;
            } else {
                vertex.texCoords = glm::vec2(0.0f, 0.0f);
            }
            
            // Tangents
            if (mesh->HasTangentsAndBitangents()) {
                vertex.tangent.x = mesh->mTangents[i].x;
                vertex.tangent.y = mesh->mTangents[i].y;
                vertex.tangent.z = mesh->mTangents[i].z;
                
                vertex.bitangent.x = mesh->mBitangents[i].x;
                vertex.bitangent.y = mesh->mBitangents[i].y;
                vertex.bitangent.z = mesh->mBitangents[i].z;
            } else {
                vertex.tangent = glm::vec3(1.0f, 0.0f, 0.0f);
                vertex.bitangent = glm::vec3(0.0f, 0.0f, 1.0f);
            }
            
            vertices.push_back(vertex);
        }
    }
    
    // Process indices
    for (unsigned int i = 0; i < mesh->mNumFaces; i++) {
        aiFace face = mesh->mFaces[i];
        for (unsigned int j = 0; j < face.mNumIndices; j++) {
            indices.push_back(face.mIndices[j]);
        }
    }
    
    // Process material
    Material material;
    if (mesh->mMaterialIndex >= 0) {
        aiMaterial* mat = scene->mMaterials[mesh->mMaterialIndex];
        material = loadMaterial(mat);
    }

    // Skinned meshes keep their bone ids/weights (issue #260); static meshes are
    // unchanged.
    if (hasBones) {
        return std::make_unique<Mesh>(std::move(animatedVertices), std::move(indices),
                                      std::move(material));
    }
    return std::make_unique<Mesh>(std::move(vertices), std::move(indices), std::move(material));
}

Material Model::loadMaterial(aiMaterial* mat) {
    Material material;
    
    // Load textures
    material.diffuse = loadMaterialTexture(mat, aiTextureType_DIFFUSE, Texture::Type::DIFFUSE);
    material.specular = loadMaterialTexture(mat, aiTextureType_SPECULAR, Texture::Type::SPECULAR);
    material.normal = loadMaterialTexture(mat, aiTextureType_HEIGHT, Texture::Type::NORMAL);
    material.height = loadMaterialTexture(mat, aiTextureType_DISPLACEMENT, Texture::Type::HEIGHT);
    material.emissive = loadMaterialTexture(mat, aiTextureType_EMISSIVE, Texture::Type::EMISSIVE);
    
    // Load material properties
    aiColor3D color;
    
    // Diffuse color
    if (mat->Get(AI_MATKEY_COLOR_DIFFUSE, color) == AI_SUCCESS) {
        material.diffuseColor = glm::vec3(color.r, color.g, color.b);
    }
    
    // Specular color
    if (mat->Get(AI_MATKEY_COLOR_SPECULAR, color) == AI_SUCCESS) {
        material.specularColor = glm::vec3(color.r, color.g, color.b);
    }
    
    // Ambient color
    if (mat->Get(AI_MATKEY_COLOR_AMBIENT, color) == AI_SUCCESS) {
        material.ambientColor = glm::vec3(color.r, color.g, color.b);
    }
    
    // Shininess
    float shininess;
    if (mat->Get(AI_MATKEY_SHININESS, shininess) == AI_SUCCESS) {
        material.shininess = shininess;
    }

    // Metallic-roughness PBR import (issue #269). A glTF-style asset provides PBR
    // factors and/or textures; when any are present, mark the material PBR so the
    // renderer picks the PBR program. Materials without them stay Blinn-Phong.
    material.albedoMap = loadMaterialTexture(mat, aiTextureType_BASE_COLOR, Texture::Type::ALBEDO);
    // glTF packs metallic-roughness in one texture; assimp exposes it under both
    // METALNESS and DIFFUSE_ROUGHNESS, so accept whichever the importer filled.
    material.metallicRoughnessMap =
        loadMaterialTexture(mat, aiTextureType_METALNESS, Texture::Type::METALLIC_ROUGHNESS);
    if (!material.metallicRoughnessMap) {
        material.metallicRoughnessMap =
            loadMaterialTexture(mat, aiTextureType_DIFFUSE_ROUGHNESS, Texture::Type::METALLIC_ROUGHNESS);
    }
    material.aoMap = loadMaterialTexture(mat, aiTextureType_AMBIENT_OCCLUSION, Texture::Type::AMBIENT_OCCLUSION);
    if (!material.aoMap) {
        material.aoMap = loadMaterialTexture(mat, aiTextureType_LIGHTMAP, Texture::Type::AMBIENT_OCCLUSION);
    }

    aiColor4D baseColor;
    const bool hasBaseColor = mat->Get(AI_MATKEY_BASE_COLOR, baseColor) == AI_SUCCESS;
    if (hasBaseColor) {
        material.albedo = glm::vec3(baseColor.r, baseColor.g, baseColor.b);
    }
    float metallicFactor = 0.0f, roughnessFactor = 0.5f;
    const bool hasMetallic = mat->Get(AI_MATKEY_METALLIC_FACTOR, metallicFactor) == AI_SUCCESS;
    const bool hasRoughness = mat->Get(AI_MATKEY_ROUGHNESS_FACTOR, roughnessFactor) == AI_SUCCESS;
    if (hasMetallic) material.metallic = metallicFactor;
    if (hasRoughness) material.roughness = roughnessFactor;

    material.isPBR = hasBaseColor || hasMetallic || hasRoughness ||
                     material.hasAlbedoTexture() || material.hasMetallicRoughnessTexture() ||
                     material.hasAoTexture();
    if (material.isPBR) {
        // Only for PBR materials, fill the normal slot from the glTF NORMALS channel
        // when the Phong-style HEIGHT normal was absent. Gating on isPBR keeps
        // Blinn-Phong materials byte-identical (their normal slot is untouched).
        if (!material.normal) {
            material.normal = loadMaterialTexture(mat, aiTextureType_NORMALS, Texture::Type::NORMAL);
        }
        LOG_INFO("Material imported as metallic-roughness PBR");
    }

    return material;
}

std::shared_ptr<Texture> Model::loadMaterialTexture(aiMaterial* mat, aiTextureType type, Texture::Type textureType) {
    if (mat->GetTextureCount(type) == 0) {
        return nullptr;
    }
    
    std::string filename = getTextureFilename(mat, type);
    if (filename.empty()) {
        return nullptr;
    }
    
    // Check cache first
    auto it = m_textureCache.find(filename);
    if (it != m_textureCache.end()) {
        return it->second;
    }
    
    // Load new texture
    std::string fullPath = m_directory + "/" + filename;
    auto texture = Texture::loadFromFileShared(fullPath, textureType);
    
    if (texture) {
        m_textureCache[filename] = texture;
        LOG_INFO("Loaded material texture: " + fullPath);
    } else {
        LOG_WARNING("Failed to load material texture: " + fullPath);
    }
    
    return texture;
}

std::string Model::getTextureFilename(aiMaterial* mat, aiTextureType type, unsigned int index) {
    aiString str;
    if (mat->GetTexture(type, index, &str) == AI_SUCCESS) {
        std::string filename = str.C_Str();
        // Remove path if present, keep only filename
        size_t lastSlash = filename.find_last_of("/\\");
        if (lastSlash != std::string::npos) {
            filename = filename.substr(lastSlash + 1);
        }
        return filename;
    }
    return "";
}

Texture::Type Model::aiTextureTypeToTextureType(aiTextureType type) {
    switch (type) {
        case aiTextureType_DIFFUSE: return Texture::Type::DIFFUSE;
        case aiTextureType_SPECULAR: return Texture::Type::SPECULAR;
        case aiTextureType_HEIGHT: return Texture::Type::NORMAL;
        case aiTextureType_DISPLACEMENT: return Texture::Type::HEIGHT;
        case aiTextureType_EMISSIVE: return Texture::Type::EMISSIVE;
        default: return Texture::Type::DIFFUSE;
    }
}

void Model::render(std::shared_ptr<Shader> shader) const {
    for (const auto& mesh : m_meshes) {
        mesh->render(shader);
    }
}

void Model::renderSelectable(std::shared_ptr<Shader> phongShader, std::shared_ptr<Shader> pbrShader,
                             const glm::mat4& model, const glm::mat3& normalMatrix) const {
    for (const auto& mesh : m_meshes) {
        // Pick the PBR program for PBR meshes (issue #269); everything else stays on
        // the Blinn-Phong path. Falls back to Phong when no PBR program is provided.
        const bool usePbr = pbrShader && mesh->getMaterial().isPBR;
        std::shared_ptr<Shader> shader = usePbr ? pbrShader : phongShader;
        if (!shader) continue;

        // Per-mesh matrices go on the chosen program (frame/light uniforms are set on
        // both by the caller). Mesh::render also calls use(), binding this program.
        shader->use();
        shader->setMat4("model", glm::value_ptr(model));
        shader->setMat3("normalMatrix", glm::value_ptr(normalMatrix));
        mesh->render(shader);
    }
}

std::shared_ptr<Model> Model::loadFromFileShared(const std::string& path) {
    auto model = std::make_shared<Model>();
    if (model->loadFromFile(path)) {
        return model;
    }
    return nullptr;
}

std::string Model::getSupportedFormats() {
    return "OBJ, FBX, GLTF/GLB, 3DS, DAE (Collada), PLY, STL, and many others supported by Assimp";
}

void Model::calculateModelBoundingBox() {
    if (m_meshes.empty()) {
        m_boundingBox = BoundingBox();
        return;
    }
    
    // Initialize with first mesh bounding box
    m_boundingBox = m_meshes[0]->getBoundingBox();
    
    // Expand to include all mesh bounding boxes
    for (size_t i = 1; i < m_meshes.size(); ++i) {
        m_boundingBox.expand(m_meshes[i]->getBoundingBox());
    }
    
    LOG_INFO("Model bounding box calculated: min(" + 
             std::to_string(m_boundingBox.min.x) + ", " + 
             std::to_string(m_boundingBox.min.y) + ", " + 
             std::to_string(m_boundingBox.min.z) + ") max(" + 
             std::to_string(m_boundingBox.max.x) + ", " + 
             std::to_string(m_boundingBox.max.y) + ", " + 
             std::to_string(m_boundingBox.max.z) + ")");
}

void Model::extractBoneWeightForVertices(std::vector<AnimatedVertex>& vertices, aiMesh* mesh, const aiScene* scene) {
    for (int boneIndex = 0; boneIndex < mesh->mNumBones; ++boneIndex) {
        int boneID = -1;
        std::string boneName = mesh->mBones[boneIndex]->mName.C_Str();
        
        if (m_boneInfoMap.find(boneName) == m_boneInfoMap.end()) {
            BoneInfo newBoneInfo;
            newBoneInfo.id = m_boneCounter;
            
            // Convert Assimp matrix to GLM matrix
            aiMatrix4x4 offsetMatrix = mesh->mBones[boneIndex]->mOffsetMatrix;
            newBoneInfo.offset = glm::mat4(
                offsetMatrix.a1, offsetMatrix.b1, offsetMatrix.c1, offsetMatrix.d1,
                offsetMatrix.a2, offsetMatrix.b2, offsetMatrix.c2, offsetMatrix.d2,
                offsetMatrix.a3, offsetMatrix.b3, offsetMatrix.c3, offsetMatrix.d3,
                offsetMatrix.a4, offsetMatrix.b4, offsetMatrix.c4, offsetMatrix.d4
            );
            
            m_boneInfoMap[boneName] = newBoneInfo;
            boneID = m_boneCounter;
            m_boneCounter++;
        } else {
            boneID = m_boneInfoMap[boneName].id;
        }
        
        assert(boneID != -1);
        auto weights = mesh->mBones[boneIndex]->mWeights;
        int numWeights = mesh->mBones[boneIndex]->mNumWeights;
        
        for (int weightIndex = 0; weightIndex < numWeights; ++weightIndex) {
            int vertexId = weights[weightIndex].mVertexId;
            float weight = weights[weightIndex].mWeight;
            assert(vertexId <= vertices.size());
            setBoneIdAndWeights(vertices[vertexId], boneID, weight);
        }
    }
}

void Model::setBoneIdAndWeights(AnimatedVertex& vertex, int boneID, float weight) {
    for (int i = 0; i < MAX_BONE_INFLUENCE; ++i) {
        if (vertex.boneIDs[i] < 0) {
            vertex.weights[i] = weight;
            vertex.boneIDs[i] = boneID;
            break;
        }
    }
}

} // namespace IKore
