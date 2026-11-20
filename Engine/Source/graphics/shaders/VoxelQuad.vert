#version 450 core

// ---------------------------------------------------------------------------
//  Voxel quad vertex shader - vertex pulling.
//
//  There are no vertex attributes for geometry. Each draw covers a run of
//  8-byte packed quads in the arena SSBO; gl_VertexID (which includes the
//  draw's baseVertex) selects the quad (>> 2) and the corner (& 3). One
//  instanced attribute carries the section record for the draw.
//
//  Quad bit layout (see voxel/types/quad.hpp):
//    lo:  0-2 face   3-6 length-1   7-10 width-1   11-15 Z   16-20 Y   21-25 X   26-31 material lo
//    hi:  0-9 material hi   10-17 corner AO
// ---------------------------------------------------------------------------

layout(location = 0) in uvec4 aRecord;   // posLod, aabb, geometryOffset, flags (divisor 1)

layout(std430, binding = 0) readonly buffer QuadBuffer     { uvec2 quads[]; };
layout(std430, binding = 1) readonly buffer MaterialBuffer { vec4 colourLinear[]; };

uniform mat4  u_viewProj;
uniform vec3  u_origin;
uniform float u_voxelSize;
uniform vec4  u_faceTint;     // up, down, z, x
uniform uint  u_flags;        // bit0 AO, bit1 LoD colours

out flat vec3  vColour;
out flat uint  vAo;
out      vec2  vCell;
out flat uvec2 vSize;

// (u, v, d) -> (x, y, z). Axis 0: u=X v=Z d=Y.  Axis 1: u=X v=Y d=Z.  Axis 2: u=Y v=Z d=X.
vec3 unswizzle(uint axis, vec3 uvd)
{
    if (axis == 0u) return vec3(uvd.x, uvd.z, uvd.y);
    if (axis == 1u) return uvd;
    return vec3(uvd.z, uvd.x, uvd.y);
}

const vec3 kLodPalette[4] = vec3[4](vec3(0.9, 0.9, 0.9), vec3(0.2, 0.9, 0.2), vec3(0.2, 0.4, 1.0), vec3(1.0, 0.3, 0.3));

void main()
{
    uvec2 q       = quads[gl_VertexID >> 2];
    uint  corner  = uint(gl_VertexID) & 3u;

    uint  face    = q.x & 7u;
    uint  axis    = face >> 1;
    uint  side    = face & 1u;
    uvec2 size    = uvec2(((q.x >> 3) & 15u) + 1u, ((q.x >> 7) & 15u) + 1u);
    uvec3 vpos    = uvec3((q.x >> 21) & 31u, (q.x >> 16) & 31u, (q.x >> 11) & 31u);
    uint  mat     = ((q.x >> 26) & 63u) | ((q.y & 1023u) << 6);
    uint  ao      = (q.y >> 10) & 255u;

    uvec3 spos    = uvec3(aRecord.x & 1023u, (aRecord.x >> 10) & 255u, (aRecord.x >> 18) & 1023u);
    uint  lod     = (aRecord.x >> 28) & 7u;
    float scale   = float(1u << lod);

    // Corners: bit0 = u, bit1 = v. The index pattern (0,1,2)(2,1,3) is
    // counter-clockwise seen from the u x v side, which is -Y, +Z and +X.
    // The other three directions swap corners 1 and 2.
    bool flip = (axis == 0u) ? (side == 1u) : (side == 0u);
    if (flip && (corner == 1u || corner == 2u)) corner ^= 3u;

    vec2 cm = vec2(corner & 1u, (corner >> 1) & 1u);

    // Owning-voxel (u, v, d) and the tiny expansion that hides seams
    // between neighbouring sections at the same level.
    vec3 uvdPos;
    if (axis == 0u)      uvdPos = vec3(vpos.x, vpos.z, vpos.y);
    else if (axis == 1u) uvdPos = vec3(vpos.x, vpos.y, vpos.z);
    else                 uvdPos = vec3(vpos.y, vpos.z, vpos.x);

    const float eps = 0.00005;
    vec3 uvd   = uvdPos + vec3(-eps + cm * (vec2(size) + 2.0 * eps), float(side));
    vec3 local = unswizzle(axis, uvd);
    vec3 world = u_origin + (local * scale + vec3(spos) * (32.0 * scale)) * u_voxelSize;

    gl_Position = u_viewProj * vec4(world, 1.0);

    float tint = (axis == 0u) ? (side == 1u ? u_faceTint.x : u_faceTint.y)
               : (axis == 1u ? u_faceTint.z : u_faceTint.w);

    vColour = colourLinear[mat].rgb * tint;
    if ((u_flags & 2u) != 0u) vColour = kLodPalette[lod & 3u] * tint;

    vAo   = ao;
    vCell = cm * vec2(size);
    vSize = size;
}
