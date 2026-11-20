#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aColor;

// Already world space - debug geometry comes from Jolt in world coordinates,
// so there is no model matrix.
uniform mat4 u_viewProj;

out vec3 vColor;

void main()
{
    gl_Position = u_viewProj * vec4(aPos, 1.0);
    vColor = aColor;
}
