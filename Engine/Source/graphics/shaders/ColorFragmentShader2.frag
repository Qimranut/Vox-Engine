#version 450 core
 
in vec3 ourWorldPos;
in vec3 ourNormal;
in vec3 ourColor;
in vec2 TexCoord;
in vec3 ourTangent; 
 
layout(binding = 0) uniform sampler2D u_albedo;
layout(binding = 2) uniform samplerCube u_irradianceMap;
layout(binding = 3) uniform samplerCube u_prefilteredMap;
layout(binding = 4) uniform sampler2D u_brdfLut;
layout(binding = 5) uniform sampler2D u_normalMap;
 
uniform vec3 u_tint;
uniform vec3 u_cameraPos;
uniform int u_hasNormalMap;
uniform bool u_lightingEnabled = true;
uniform bool u_transparent2D = false;
 
// Directional light: direction from the surface toward the light.
uniform vec3 u_lightDir;
uniform vec3 u_lightColor;
uniform float u_lightIntensity;
 
// Metallic-roughness material. Fixed for now.
uniform float u_metallic;
uniform float u_roughness;
uniform float u_ao;
 
out vec4 FragColor;
 
const float PI = 3.14159265359;
 
vec3 linearToSrgb(vec3 c)
{
    c = clamp(c, 0.0, 1.0);
    vec3 low = c * 12.92;
    vec3 high = 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055;
    return mix(high, low, lessThanEqual(c, vec3(0.0031308)));
}
 
float DistributionGGX(vec3 N, vec3 H, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;
 
    float denom = NdotH2 * (a2 - 1.0) + 1.0;
    return a2 / max(PI * denom * denom, 0.0001);
}
 
float GeometrySchlickGGX(float NdotV, float roughness)
{
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return NdotV / max(NdotV * (1.0 - k) + k, 0.0001);
}
 
float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness)
{
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    return GeometrySchlickGGX(NdotV, roughness) *
           GeometrySchlickGGX(NdotL, roughness);
}
 
vec3 FresnelSchlick(float cosTheta, vec3 F0)
{
    return F0 + (vec3(1.0) - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}
 
void main()
{
    vec4 texel = texture(u_albedo, TexCoord);
    if (u_transparent2D && texel.a <= 0.0) discard;
    vec3 albedo = texel.rgb * u_tint;
    if (!u_lightingEnabled)
    {
        FragColor = vec4(linearToSrgb(albedo), u_transparent2D ? texel.a : 1.0);
        return;
    }
    float metallic = clamp(u_metallic, 0.0, 1.0);
    float roughness = clamp(u_roughness, 0.04, 1.0);
    float ao = clamp(u_ao, 0.0, 1.0);
 
    vec3 N = normalize(ourNormal);
    if (u_hasNormalMap == 1)
    {
        vec3 tangentNormal = texture(u_normalMap, TexCoord).rgb * 2.0 - 1.0;
        tangentNormal.z = sqrt(max(0.0, 1.0 - dot(tangentNormal.xy, tangentNormal.xy)));
        vec3 T = normalize(ourTangent);
        vec3 B = cross(N, T);
        mat3 TBN = mat3(T, B, N);
        N = normalize(TBN * tangentNormal);
    }
    vec3 V = normalize(u_cameraPos - ourWorldPos);
    vec3 L = normalize(u_lightDir);
    vec3 H = normalize(V + L);
 
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
 
    vec3 F0 = mix(vec3(0.04), albedo, metallic);
    vec3 F = FresnelSchlick(max(dot(H, V), 0.0), F0);
 
    float NDF = DistributionGGX(N, H, roughness);
    float G = GeometrySmith(N, V, L, roughness);
 
    vec3 specular = (NDF * G * F) / max(4.0 * NdotV * NdotL, 0.0001);
 
    vec3 kS = F;
    vec3 kD = (vec3(1.0) - kS) * (1.0 - metallic);
 
    vec3 diffuse = kD * albedo / PI;
    vec3 radiance = u_lightColor * u_lightIntensity;
 
    vec3 directLight = (diffuse + specular) * radiance * NdotL;
 
   // IBL — diffuse ambient from irradiance, specular from prefiltered + BRDF LUT.
    vec3 irradiance = texture(u_irradianceMap, N).rgb;
    vec3 kD_ibl = (vec3(1.0) - F) * (1.0 - metallic);
    vec3 diffuseIBL = irradiance * albedo * kD_ibl;
 
    vec3 R = reflect(-V, N);
    const float MAX_REFLECTION_LOD = 4.0;
    vec3 prefilteredColor = textureLod(u_prefilteredMap, R, roughness * MAX_REFLECTION_LOD).rgb;
    vec2 brdf = texture(u_brdfLut, vec2(NdotV, roughness)).rg;
    vec3 specularIBL = prefilteredColor * (F * brdf.x + brdf.y);
 
    vec3 ambient = (diffuseIBL + specularIBL) * ao;
 
    vec3 color = ambient + directLight;

    // ACES filmic tone mapping (Narkowicz approximation).
    // Compresses HDR values > 1.0 into a displayable range before sRGB.
    color = (color * (2.51 * color + 0.03)) /
        (color * (2.43 * color + 0.59) + 0.14);
    color = clamp(color, 0.0, 1.0);

    FragColor = vec4(linearToSrgb(color), 1.0);
}
