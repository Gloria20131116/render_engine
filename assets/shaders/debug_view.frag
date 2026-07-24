#version 450 core
// Texture inspector: channel isolation, range remap, mip/cube-face selection.
in vec2 vUV;
out vec4 FragColor;

uniform sampler2D uTex2D;
uniform samplerCube uTexCube;
uniform int uIsCube;
uniform int uFace;      // cubemap face 0..5
uniform float uMip;
uniform int uChannel;   // 0=RGB 1=R 2=G 3=B 4=A
uniform vec2 uRange;    // remap [min,max] -> [0,1]
uniform int uGamma;
uniform int uFlipY;
uniform int uIsDepth;

vec3 cubeDir(int face, vec2 uv) {
    vec2 c = uv * 2.0 - 1.0;
    if (face == 0) return vec3( 1.0, -c.y, -c.x);
    if (face == 1) return vec3(-1.0, -c.y,  c.x);
    if (face == 2) return vec3( c.x,  1.0,  c.y);
    if (face == 3) return vec3( c.x, -1.0, -c.y);
    if (face == 4) return vec3( c.x, -c.y,  1.0);
    return vec3(-c.x, -c.y, -1.0);
}

void main() {
    vec2 uv = vUV;
    if (uFlipY == 0) uv.y = 1.0 - uv.y;  // ImGui images are top-left origin

    vec4 texel = uIsCube == 1 ? textureLod(uTexCube, cubeDir(uFace, uv), uMip)
                              : textureLod(uTex2D, uv, uMip);

    vec4 c = texel;
    float lo = uRange.x, hi = max(uRange.y, uRange.x + 1e-5);
    c.rgb = (c.rgb - lo) / (hi - lo);
    c.a = (c.a - lo) / (hi - lo);

    vec3 outColor;
    if (uChannel == 1)      outColor = vec3(c.r);
    else if (uChannel == 2) outColor = vec3(c.g);
    else if (uChannel == 3) outColor = vec3(c.b);
    else if (uChannel == 4) outColor = vec3(c.a);
    else                    outColor = c.rgb;

    if (uIsDepth == 1) outColor = vec3(c.r);
    if (uGamma == 1) outColor = pow(clamp(outColor, 0.0, 1.0), vec3(1.0 / 2.2));
    FragColor = vec4(outColor, 1.0);
}
