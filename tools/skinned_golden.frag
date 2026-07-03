#version 330 core
// Minimal deterministic fragment for the skinned golden-image test (issue #287 / #290
// follow-up). It pairs with skinned_phong.vert (whose vertex skinning is what we are
// verifying) and shades by the skinned normal under one fixed directional light, so the
// output is a deterministic function of the deformed geometry - no textures, no shadow
// maps, no material state.
in vec3 FragPos;
in vec3 Normal;

out vec4 FragColor;

uniform vec3 lightDir;

void main() {
    vec3 N = normalize(Normal);
    float diff = max(dot(N, normalize(lightDir)), 0.0);
    // Tint by world position so the deformed geometry's orientation shows up as a spatial
    // gradient: a Z-axis bone rotation moves the vertices, so the pattern rotates with the
    // pose, making the golden sensitive to the exact skinning result (not just the outline).
    float t = clamp(0.5 + 0.5 * FragPos.x, 0.0, 1.0);
    float u = clamp(FragPos.y, 0.0, 1.0);
    vec3 base = mix(vec3(0.20, 0.70, 0.45), vec3(0.90, 0.40, 0.30), t);
    base = mix(base, vec3(0.30, 0.45, 0.95), 0.5 * u);
    vec3 color = base * (0.30 + 0.70 * diff);
    color = pow(clamp(color, 0.0, 1.0), vec3(1.0 / 2.2));
    FragColor = vec4(color, 1.0);
}
