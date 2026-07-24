#version 450 core
// Unit cube generated from gl_VertexID; used for cubemap capture & skybox.
uniform mat4 uView;
uniform mat4 uProj;

out vec3 vLocalPos;

// Corner numbering is Z-order: bit0 = +x, bit1 = +y, bit2 = +z.
const int indices[36] = int[](
    0,1,3, 0,3,2,   4,6,7, 4,7,5,
    0,2,6, 0,6,4,   1,5,7, 1,7,3,
    0,4,5, 0,5,1,   2,3,7, 2,7,6);

vec3 corner(int i) {
    return vec3((i & 1) != 0 ? 1.0 : -1.0,
                (i & 2) != 0 ? 1.0 : -1.0,
                (i & 4) != 0 ? 1.0 : -1.0);
}

void main() {
    vec3 pos = corner(indices[gl_VertexID]);
    vLocalPos = pos;
    gl_Position = uProj * uView * vec4(pos, 1.0);
}
