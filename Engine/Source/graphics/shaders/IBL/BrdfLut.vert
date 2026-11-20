#version 450 core
 
// Fullscreen triangle: 3 vertices, no VBO needed.
// gl_VertexID 0 -> (-1, -1)
// gl_VertexID 1 -> ( 3, -1)
// gl_VertexID 2 -> (-1,  3)
out vec2 vTexCoord;
 
void main()
{
    vec2 positions[3] = vec2[3](
        vec2(-1.0, -1.0),
        vec2( 3.0, -1.0),
        vec2(-1.0,  3.0)
    );
 
    vec2 p = positions[gl_VertexID];
    gl_Position = vec4(p, 0.0, 1.0);
 
    // UV maps the visible triangle to [0, 1].
    vTexCoord = p * 0.5 + 0.5;
}