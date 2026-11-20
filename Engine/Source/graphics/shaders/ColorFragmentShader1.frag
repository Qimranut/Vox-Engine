#version 450 core
in vec3 ourNormal;
in vec3 ourColor;
in vec2 TexCoord;
out vec4 FragColor;

// Albedo, from an SRGB8_ALPHA8 / BCn sRGB texture: the GPU hands back LINEAR
// values, decoded before filtering. Untextured draws bind a 1x1 white texture,
// so there is no "has texture" branch. Unit mirrors TextureUnit::Albedo.
layout(binding = 0) uniform sampler2D u_albedo;

// Per-object colour from cpn::Visibility, already converted to linear on the
// CPU. The face colours stay in the vertex data for anything that wants them.
uniform vec3 u_tint;
// The saved scene mode selects this each frame: menus are unlit, 3D is lit.
uniform bool u_lightingEnabled = true;
uniform bool u_transparent2D = false;

// Fixed key light plus ambient: enough shading to read a box's shape and
// orientation, which flat colour cannot show once it starts tumbling. Lighting
// is linear, where 0.12 ambient displays roughly as bright as 0.35 did when
// this shader lit in gamma space.
const vec3 kLightDir = normalize(vec3(0.4, 1.0, 0.3));
const float kAmbient = 0.12;

// Exact piecewise sRGB encode, not pow(1/2.2), which crushes the darks.
// Kept as the one place linear becomes display-referred, so it can move to a
// tonemap pass unchanged once the scene renders to an HDR target.
vec3 linearToSrgb(vec3 c)
{
    c = clamp(c, 0.0, 1.0);
    vec3 low  = c * 12.92;
    vec3 high = 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055;
    return mix(high, low, lessThanEqual(c, vec3(0.0031308)));
}

// Tangent-space normal from a BC5 normal map. BC5 stores only X and Y (red,
// green); blue samples as 0. The normal is unit length, so
// Z = sqrt(1 - X^2 - Y^2). OpenGL (+Y up) green convention, as TextureCompiler
// writes it. Mirrored by graphics::texture::decodeNormalBC5 for the texture
// self-check. Unused until the material gains a normal slot.
vec3 decodeNormalBC5(vec2 rg)
{
    vec2 xy = rg * 2.0 - 1.0;
    float z = sqrt(max(0.0, 1.0 - dot(xy, xy)));
    return normalize(vec3(xy, z));
}

void main()
{
    vec4 texel = texture(u_albedo, TexCoord);
    if (u_transparent2D && texel.a <= 0.0) discard;
    vec3 albedo = texel.rgb * u_tint;

    float light = 1.0;
    if (u_lightingEnabled)
    {
        float lambert = max(dot(normalize(ourNormal), kLightDir), 0.0);
        light = kAmbient + (1.0 - kAmbient) * lambert;
    }

    // Preserve coverage for menu blending; 3D remains opaque.
    FragColor = vec4(linearToSrgb(albedo * light), u_transparent2D ? texel.a : 1.0);
}
