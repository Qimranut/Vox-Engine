#version 450 core
 
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec3 aColor;
layout(location = 3) in vec2 aTexCoord;
layout(location = 4) in vec3 aTangent;
 
uniform mat4 u_viewProj;
uniform mat4 u_model;
uniform mat3 u_normalMatrix;
 
out vec3 ourWorldPos;
out vec3 ourNormal;
out vec3 ourColor;
out vec2 TexCoord;
out vec3 ourTangent;
 
void main()
{
    vec4 worldPos = u_model * vec4(aPos, 1.0);
 
    gl_Position = u_viewProj * worldPos;
 
    ourWorldPos = worldPos.xyz;
    ourNormal = normalize(u_normalMatrix * aNormal);
    ourTangent = normalize(u_normalMatrix * aTangent);
    ourColor = aColor;
    TexCoord = aTexCoord;
}