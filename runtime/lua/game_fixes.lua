-- Fixes for two leaks in the shipped game. On a PC they cost nothing anyone notices. On the
-- PS3's interpreter each leaked object is updated and drawn every frame until the run ends,
-- so a long run slows down round by round and its heap grows by about 1 MB a round.
--
-- 1. UIElement:set_values (engine/ui.lua) runs Moveable.init again on an element that already
--    exists, which appends it to G.MOVEABLES and the stage list a second time. remove() takes
--    out only the first copy, so the second stays. The fix drops the second registration.
-- 2. The Cash Out button (functions/common_events.lua) is a UIBox bonded to G.round_eval but
--    stored nowhere, so removing G.round_eval leaves it behind. The fix removes any UIBox
--    whose major is the UIBox being removed.
-- 3. Blind:defeat (blind.lua) puts new dissolve particles in self.children.particles each
--    round without removing the old ones, which stopped emitting long before. The fix
--    removes the old ones first.
--
-- harness/README.md shows how to measure all three, and HARNESS_MAX_GROWTH_KB checks them.
local registered = setmetatable({}, { __mode = 'k' })

-- Takes `object` off the end of `list` if a registration just appended it there.
local function drop_last(list, object)
    if list and list[#list] == object then list[#list] = nil end
end

local function fix_double_registration()
    local native_init = Moveable.init
    Moveable.init = function(self, ...)
        local again = registered[self]
        local results = { native_init(self, ...) }
        registered[self] = true
        if again then
            drop_last(G.MOVEABLES, self)
            if not G.STAGE_OBJECT_INTERRUPT then drop_last(G.STAGE_OBJECTS[G.STAGE], self) end
        end
        return unpack(results)
    end
end

local function fix_orphaned_boxes()
    local native_remove = UIBox.remove
    UIBox.remove = function(self, ...)
        local results = { native_remove(self, ...) }
        local stage = G.STAGE_OBJECTS[G.STAGE] or {}
        for index = #stage, 1, -1 do
            local object = stage[index]
            if object and object ~= self and not object.REMOVED and getmetatable(object) == UIBox
                and object.role and object.role.major == self then
                object:remove()
            end
        end
        return unpack(results)
    end
end

local function fix_blind_particles()
    local native_defeat = Blind.defeat
    Blind.defeat = function(self, ...)
        local old_particles = self.children.particles
        if old_particles and not old_particles.REMOVED then old_particles:remove() end
        self.children.particles = nil
        return native_defeat(self, ...)
    end
end

return function()
    fix_double_registration()
    fix_orphaned_boxes()
    fix_blind_particles()
end
