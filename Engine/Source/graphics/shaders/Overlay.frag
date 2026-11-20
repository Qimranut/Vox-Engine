#version 450 core
in vec3 vNormal;
in vec4 vColour;

// Unit vector from the scene toward the camera: solids are lit from where
// the viewer stands, so a handle reads as round from any side.
uniform vec3 u_toEye;

out vec4 FragColor;

void main()
{
    // Colours arrive display-referred and leave the same way, like the debug
    // lines: the scene pass has already encoded this framebuffer.
    float lit = 1.0;
    if (dot(vNormal, vNormal) > 0.25)
        lit = 0.62 + 0.38 * max(dot(normalize(vNormal), u_toEye), 0.0);

    FragColor = vec4(vColour.rgb * lit, vColour.a);
}
