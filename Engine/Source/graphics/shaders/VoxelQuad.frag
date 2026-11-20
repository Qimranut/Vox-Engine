#version 450 core

in flat vec3  vColour;
in flat uint  vAo;
in      vec2  vCell;
in flat uvec2 vSize;

uniform uint u_flags;   // bit0 AO

out vec4 FragColor;

// Same encode as ColorFragmentShader1.frag: the one place linear becomes
// display-referred. GL_FRAMEBUFFER_SRGB stays off.
vec3 linearToSrgb(vec3 c)
{
    c = clamp(c, 0.0, 1.0);
    vec3 low  = c * 12.92;
    vec3 high = 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055;
    return mix(high, low, lessThanEqual(c, vec3(0.0031308)));
}

void main()
{
    vec3 c = vColour;

    if ((u_flags & 1u) != 0u)
    {
        // Two bits per corner, corner k = (uBit << 1) | vBit; the same
        // tuple applies to every voxel cell of a merged quad.
        vec2  f   = fract(min(vCell, vec2(vSize) - 0.0001));
        float a00 = float( vAo       & 3u);
        float a01 = float((vAo >> 2) & 3u);
        float a10 = float((vAo >> 4) & 3u);
        float a11 = float((vAo >> 6) & 3u);
        float a   = mix(mix(a00, a01, f.y), mix(a10, a11, f.y), f.x) / 3.0;
        c *= mix(0.55, 1.0, a);
    }

    FragColor = vec4(linearToSrgb(c), 1.0);
}
