--[[
/*************************************************************************************************************
\file       voxel_test.lua
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Voxel terrain smoke test. G: print the material under the crosshair. Voxel.Raycast walks the
            voxel grid itself, so it works in the Editor too, where the terrain has no collision mesh
            until Play is pressed.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/
]]

--[[!
    @brief
        The camera's position and forward direction.
    @return
        cx, cy, cz, dx, dy, dz: ray origin and direction
    ]]
local function cameraRay()
    local cx, cy, cz = Camera.GetPosition()
    local yaw, pitch = Camera.GetYawPitch()
    local y, p = math.rad(yaw), math.rad(pitch)
    -- Camera convention: forward = (-sin(yaw)cos(pitch), sin(pitch), -cos(yaw)cos(pitch))
    local dx = -math.sin(y) * math.cos(p)
    local dy =  math.sin(p)
    local dz = -math.cos(y) * math.cos(p)
    return cx, cy, cz, dx, dy, dz
end

--[[!
    @brief
        The voxel under the crosshair, found with a 200 m grid raycast.
    @return
        x, y, z of the hit voxel, or nil when there is no world or no hit
    ]]
local function lookedAtVoxel()
    if not Voxel.HasWorld() then return nil end
    local ox, oy, oz, dx, dy, dz = cameraRay()
    local hit, x, y, z = Voxel.Raycast(ox, oy, oz, dx, dy, dz, 200)
    if not hit then return nil end
    return x, y, z
end

--[[!
    @brief
        Script start-up: binds G to print the looked-at voxel's material and the column's surface height.
    ]]
function OnCreate()
    Input.OnKey(Key.G, "Pressed", function(mods)
        local x, y, z = lookedAtVoxel()
        if not x then print("[voxel_test] not looking at terrain") return end
        local id = Voxel.GetMaterial(x, y, z)
        print(string.format("[voxel_test] voxel (%d, %d, %d) is %s (id %d), surface at y = %d",
            x, y, z, Voxel.MaterialName(id), id, Voxel.SurfaceHeight(x, z)))
    end)
end
