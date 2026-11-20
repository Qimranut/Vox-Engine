#version 450 core
 
in vec3 vWorldPos;
 
out vec4 FragColor;
 
const float PI = 3.14159265359;
 
layout(binding = 1) uniform samplerCube u_skybox;
 
void main()
{
    vec3 normal = normalize(vWorldPos);
 
    vec3 irradiance = vec3(0.0);
 
    // Tangent/bitangent for building a TBN frame around the normal.
    vec3 up = abs(normal.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(0.0, 0.0, 1.0);
    vec3 tangent = normalize(cross(up, normal));
    vec3 bitangent = cross(normal, tangent);
 
    float sampleCount = 0.0;
 
    // Hemisphere convolution: phi in [0, 2pi), theta in [0, pi/2).
    // Coarse sampling is fine — irradiance is low-frequency.
    const float SAMPLE_DELTA = 0.025;
    for (float phi = 0.0; phi < 2.0 * PI; phi += SAMPLE_DELTA)
    {
        for (float theta = 0.0; theta < 0.5 * PI; theta += SAMPLE_DELTA)
        {
            // Spherical to cartesian in tangent space.
            vec3 tangentSample = vec3(
                sin(theta) * cos(phi),
                sin(theta) * sin(phi),
                cos(theta)
            );
 
            // Tangent space to world.
            vec3 sampleVec = tangent * tangentSample.x
                           + bitangent * tangentSample.y
                           + normal * tangentSample.z;
 
            irradiance += texture(u_skybox, sampleVec).rgb
                        * cos(theta) * sin(theta);
 
            sampleCount += 1.0;
        }
    }
 
    irradiance = PI * irradiance / sampleCount;
 
    FragColor = vec4(irradiance, 1.0);
}