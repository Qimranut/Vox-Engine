--[[
/*************************************************************************************************************
\file       test.lua
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Example gameplay script: a few globals, an R key that creates scene nodes, and a log line
            when the primary gamepad changes.

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/
]]

Health  = 100;
Mana    = 50;
Name    = "Player"

-- ---------------------------------------------------------------------------
-- Input is event-driven: register callbacks, don't poll.
--
-- Subscribe from OnCreate, never at the top level of the file. Each Input.On*
-- call adds a subscription that lasts until Input.Off(handle) or a script
-- reload, so top-level code that runs more than once stacks duplicates.
-- ---------------------------------------------------------------------------

--[[!
    @brief
        Script start-up: subscribes the R key (creates two nodes) and the gamepad-changed log.
    ]]
function OnCreate()
    print("Oncreate from test.lua has been invoked");

    -- Raw keys. These hardcode a binding and never fire on a gamepad -
    -- prefer actions (Input.OnAction) where one exists.
    Input.OnKey(Key.R, "Released", function(mods)
        Node.Create();
        Node.Create("Default")
    end)

    Input.OnGamepad("PrimaryChanged", function(slot, name)
        if slot >= 0 then
            print("Now playing on " .. name);
        else
            print("Gamepad disconnected");
        end
    end)
end
