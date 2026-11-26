-- The server passes confirmed kills and the player's current room. Room totals
-- follow the defeated monster's room, even if the player is already elsewhere.
local M = {}
local players = {}
local normal, elite = 1, 2

function M.Update(session, currentRoom, victimRoom, kind)
    local tally = players[session]
    if kind == normal or kind == elite then
        if not tally then
            tally = {world = {0, 0}, rooms = {}}
            players[session] = tally
        end
        tally.world[kind] = tally.world[kind] + 1
        if victimRoom ~= "" then
            local room = tally.rooms[victimRoom] or {0, 0}
            room[kind] = room[kind] + 1
            tally.rooms[victimRoom] = room
        end
    end

    if not tally then
        return 0, 0, 0, 0
    end
    local room = tally.rooms[currentRoom]
    return tally.world[normal], tally.world[elite], room and room[normal] or 0, room and room[elite] or 0
end

function M.Forget(session)
    players[session] = nil
end

return M
