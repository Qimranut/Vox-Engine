#version 450 core
 
in vec3 vDirection;
 
layout(binding = 1) uniform samplerCube u_skybox;
 
out vec4 FragColor;
 
void main()
{
    FragColor = texture(u_skybox, normalize(vDirection));
}