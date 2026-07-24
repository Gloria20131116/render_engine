#version 450 core
// Fallback environment when no HDR is loaded: simple gradient sky + ground.
in vec3 vLocalPos;
out vec4 FragColor;

uniform float uRotation;

void main() {
    vec3 dir = normalize(vLocalPos);
    vec3 zenith  = vec3(0.18, 0.34, 0.65);
    vec3 horizon = vec3(0.72, 0.80, 0.92);
    vec3 ground  = vec3(0.22, 0.20, 0.19);

    float t = dir.y;
    vec3 color;
    if (t >= 0.0) {
        color = mix(horizon, zenith, pow(clamp(t, 0.0, 1.0), 0.55));
    } else {
        color = mix(horizon, ground, pow(clamp(-t, 0.0, 1.0), 0.4));
    }

    // Soft sun glow baked into the environment
    vec3 sunDir = normalize(vec3(cos(uRotation + 0.8), 0.6, sin(uRotation + 0.8)));
    float sunDot = max(dot(dir, sunDir), 0.0);
    color += vec3(1.0, 0.9, 0.7) * pow(sunDot, 350.0) * 40.0;
    color += vec3(1.0, 0.85, 0.6) * pow(sunDot, 8.0) * 0.22;

    FragColor = vec4(color, 1.0);
}
