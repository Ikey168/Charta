#version 330 core
out vec4 FragColor;

in vec3 FragPos;
in vec3 Normal;
in vec2 TexCoord;
in mat3 TBN;

// Metallic-roughness material (issue #234). This fragment path is only used by
// materials that opt in; Blinn-Phong materials use phong_shadows.frag unchanged.
struct Material {
    vec3 albedo;
    float metallic;
    float roughness;
    float ao;
};

// Textured metallic-roughness (issue #269). Each map is optional; when absent the
// corresponding use* flag is false and the scalar factor above is used unchanged.
// glTF packs roughness in the G channel and metallic in the B channel of the
// metallic-roughness texture, and occlusion in the R channel of the AO texture.
uniform sampler2D albedoMap;
uniform sampler2D metallicRoughnessMap;
uniform sampler2D aoMap;
uniform sampler2D normalMap;
uniform bool useAlbedoMap;
uniform bool useMetallicRoughnessMap;
uniform bool useAoMap;
uniform bool useNormalMap;

struct DirectionalLight {
    vec3 direction; // direction the light travels
    vec3 diffuse;   // light color / intensity
};

struct PointLight {
    vec3 position;
    vec3 diffuse;
    float constant;
    float linear;
    float quadratic;
};

#define MAX_POINT_LIGHTS 4

uniform Material material;
uniform DirectionalLight dirLight;
uniform PointLight pointLights[MAX_POINT_LIGHTS];
uniform int numPointLights;
uniform bool useDirLight;
uniform vec3 viewPos;

// Image-based lighting (issue #270). When useIBL is false these are ignored and the
// ambient term is the constant fallback below, byte-identical to before. When true the
// ambient term samples the irradiance map (diffuse), the prefiltered environment
// (specular), and the split-sum BRDF LUT, mirroring render::IblBrdf.
uniform samplerCube irradianceMap;
uniform samplerCube prefilterMap;
uniform sampler2D brdfLUT;
uniform bool useIBL;
uniform float maxReflectionLod;
// Exposure/intensity scale for the IBL ambient (issue #288); 1.0 leaves #270 unchanged.
uniform float iblIntensity;

const float PI = 3.14159265359;

// Roughness-aware Fresnel for the ambient term: rough surfaces keep more of their
// specular at grazing angles clamped by F0 (Sebastien Lagarde).
vec3 fresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness) {
    return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// The functions below mirror src/render/PbrBrdf.h and the reference evaluation in
// src/render/PbrMaterial.h (evaluateDirectionalPbr), which are unit-tested. GLSL is
// not compiled in CI, so keeping the math identical to the tested C++ is the
// safeguard against drift.
float distributionGGX(vec3 N, vec3 H, float roughness) {
    float r = clamp(roughness, 1e-3, 1.0);
    float a = r * r;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float nh2 = NdotH * NdotH;
    // Written to avoid catastrophic cancellation (matches PbrBrdf.h).
    float d = nh2 * a2 + (1.0 - nh2);
    return a2 / (PI * d * d);
}

float geometrySchlickGGX(float NdotX, float k) {
    float denom = NdotX * (1.0 - k) + k;
    return denom > 0.0 ? NdotX / denom : 0.0;
}

float geometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {
    float r = clamp(roughness, 0.0, 1.0);
    float k = (r + 1.0) * (r + 1.0) / 8.0; // direct-lighting remap
    float ggxV = geometrySchlickGGX(max(dot(N, V), 0.0), k);
    float ggxL = geometrySchlickGGX(max(dot(N, L), 0.0), k);
    return ggxV * ggxL;
}

vec3 fresnelSchlick(float cosTheta, vec3 F0) {
    float m = clamp(1.0 - cosTheta, 0.0, 1.0);
    float m5 = m * m * m * m * m;
    return F0 + (1.0 - F0) * m5;
}

// Cook-Torrance contribution of a single light. Mirrors evaluateDirectionalPbr().
vec3 shadePbr(vec3 N, vec3 V, vec3 L, vec3 radiance, vec3 albedo, float metallic, float roughness) {
    float NdotL = max(dot(N, L), 0.0);
    if (NdotL <= 0.0) return vec3(0.0);
    float NdotV = max(dot(N, V), 0.0);
    vec3 H = normalize(V + L);
    float VdotH = max(dot(V, H), 0.0);

    vec3 F0 = mix(vec3(0.04), albedo, metallic);
    float D = distributionGGX(N, H, roughness);
    float G = geometrySmith(N, V, L, roughness);
    vec3 F = fresnelSchlick(VdotH, F0);

    vec3 specular = (D * G * F) / (4.0 * NdotV * NdotL + 1e-4);
    vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);
    vec3 diffuse = kD * albedo / PI;
    return (diffuse + specular) * radiance * NdotL;
}

void main() {
    // Surface normal: perturb by the normal map when bound (tangent space via TBN),
    // else the interpolated vertex normal. Mirrors phong_shadows.frag's normal path.
    vec3 N;
    if (useNormalMap) {
        vec3 tangentNormal = texture(normalMap, TexCoord).rgb * 2.0 - 1.0;
        N = normalize(TBN * tangentNormal);
    } else {
        N = normalize(Normal);
    }
    vec3 V = normalize(viewPos - FragPos);

    // Combine scalar factors with the textures the glTF way (factor * channel, a
    // missing map passing 1). Mirrors render::resolvePbrInputs in PbrMaterial.h.
    vec3 albedoTexel = useAlbedoMap ? texture(albedoMap, TexCoord).rgb : vec3(1.0);
    vec2 mrTexel = useMetallicRoughnessMap ? texture(metallicRoughnessMap, TexCoord).gb : vec2(1.0);
    float aoTexel = useAoMap ? texture(aoMap, TexCoord).r : 1.0;

    vec3 albedo = material.albedo * albedoTexel;
    float roughness = material.roughness * mrTexel.x; // G channel
    float metallic = material.metallic * mrTexel.y;   // B channel
    float ao = material.ao * aoTexel;

    vec3 Lo = vec3(0.0);
    if (useDirLight) {
        vec3 L = normalize(-dirLight.direction);
        Lo += shadePbr(N, V, L, dirLight.diffuse, albedo, metallic, roughness);
    }
    for (int i = 0; i < numPointLights && i < MAX_POINT_LIGHTS; ++i) {
        vec3 toLight = pointLights[i].position - FragPos;
        float dist = length(toLight);
        vec3 L = toLight / max(dist, 1e-4);
        float attenuation = 1.0 / (pointLights[i].constant + pointLights[i].linear * dist +
                                   pointLights[i].quadratic * dist * dist);
        vec3 radiance = pointLights[i].diffuse * attenuation;
        Lo += shadePbr(N, V, L, radiance, albedo, metallic, roughness);
    }

    // Ambient term. With IBL (issue #270): diffuse from the irradiance map and
    // specular from the prefiltered environment weighted by the split-sum BRDF LUT.
    // Without an environment (useIBL false), fall back to the constant ambient so the
    // output matches the pre-IBL look exactly.
    vec3 ambient;
    if (useIBL) {
        float NdotV = max(dot(N, V), 0.0);
        vec3 F0 = mix(vec3(0.04), albedo, metallic);
        vec3 F = fresnelSchlickRoughness(NdotV, F0, roughness);
        vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);

        vec3 irradiance = texture(irradianceMap, N).rgb;
        vec3 diffuseIBL = irradiance * albedo;

        vec3 R = reflect(-V, N);
        vec3 prefiltered = textureLod(prefilterMap, R, roughness * maxReflectionLod).rgb;
        vec2 envBRDF = texture(brdfLUT, vec2(NdotV, roughness)).rg;
        vec3 specularIBL = prefiltered * (F * envBRDF.x + envBRDF.y);

        ambient = (kD * diffuseIBL + specularIBL) * ao * iblIntensity;
    } else {
        ambient = vec3(0.03) * albedo * ao;
    }
    vec3 color = ambient + Lo;

    // Reinhard tone map + gamma for display.
    color = color / (color + vec3(1.0));
    color = pow(color, vec3(1.0 / 2.2));
    FragColor = vec4(color, 1.0);
}
