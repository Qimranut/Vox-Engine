#version 450 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;   // zero for flat shapes
layout (location = 2) in vec4 aColour;   // display-referred sRGB, straight alpha

// Overlay geometry is queued in world space, so there is no model matrix.
uniform mat4 u_viewProj;

out vec3 vNormal;
out vec4 vColour;

void main()
{
    gl_Position = u_viewProj * vec4(aPos, 1.0);
    vNormal = aNormal;
    vColour = aColour;
}
