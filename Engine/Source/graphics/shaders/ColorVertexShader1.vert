#version 450 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec3 aColor;
layout (location = 3) in vec2 aTexCoord;

// Split rather than a single MVP: the camera changes once per frame while
// the model matrix changes per object, and a world-space normal / position
// cannot be reconstructed from a combined MVP at all.
uniform mat4 u_viewProj;
uniform mat4 u_model;
uniform vec3 u_cameraPos;

out vec3 ourNormal;
out vec3 ourWorldPos;
out vec3 ourColor;
out vec2 TexCoord; // top-left origin: v = 0 is the image's first row

void main()
{
    vec4 worldPos = u_model * vec4(aPos, 1.0);

    gl_Position = u_viewProj * worldPos;

    // TODO: use a dedicated normal matrix once non-uniform scale is a thing
    ourNormal   = mat3(u_model) * aNormal;
    ourWorldPos = worldPos.xyz;
    ourColor    = aColor;
    TexCoord    = aTexCoord;
}
