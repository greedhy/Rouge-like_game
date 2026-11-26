local Score = require("Gameplay.PlayerKillScore")

local function expect(player, room, worldNormal, worldElite, roomNormal, roomElite)
    local actual = table.pack(Score.Update(player, room, "", 0))
    local expected = {worldNormal, worldElite, roomNormal, roomElite}
    for index, value in ipairs(expected) do
        assert(actual[index] == value, ("%s/%s column %d: expected %d, got %s"):format(
            player, room, index, value, tostring(actual[index])))
    end
end

expect("player1", "room_a", 0, 0, 0, 0)
Score.Update("player1", "room_a", "room_a", 1)
Score.Update("player1", "room_a", "room_a", 2)
Score.Update("player1", "room_a", "room_b", 1)
Score.Update("player1", "room_a", "", 1)
expect("player1", "room_a", 3, 1, 1, 1)
expect("player1", "room_b", 3, 1, 1, 0)
expect("player1", "", 3, 1, 0, 0)
expect("player1", "room_a", 3, 1, 1, 1)

Score.Update("player2", "room_a", "room_a", 2)
expect("player2", "room_a", 0, 1, 0, 1)
expect("player1", "room_a", 3, 1, 1, 1)
Score.Forget("player1")
expect("player1", "room_a", 0, 0, 0, 0)
print("Player kill score Lua checks passed")
