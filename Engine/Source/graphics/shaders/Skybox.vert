#version 450 core
 
layout(location = 0) in vec3 aPos;
 
uniform mat4 u_projection;
uniform mat4 u_view;  // view matrix with translation removed
 
out vec3 vDirection;
 
void main()
{
    vDirection = aPos;
 
    // z = w forces depth to 1.0 (far plane), so the skybox always
    // sits behind everything without writing to the depth buffer.
    vec4 clipPos = u_projection * u_view * vec4(aPos, 1.0);
    gl_Position = clipPos.xyww;
}