#version 450 core
layout(location = 0) in vec3 aPosition;

uniform mat4 uLightMatrix;
uniform mat4 uModel;

void main() {
    gl_Position = uLightMatrix * uModel * vec4(aPosition, 1.0);
}
