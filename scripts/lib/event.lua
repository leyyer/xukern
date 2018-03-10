require("class")
local M = class()

function M:constructor()
	self.event = {}
end

function M:on(e, f)
	local xe = self.event[e] or {}
	table.insert(xe, f)
	self.event[e] = xe 
end

function M:remove(e, f)
	local xe = self.event[e] or {}
	for k, v in ipairs(xe) do
		if v == f then
			table.remove(xe, k)
			break
		end
	end
	self.event[e] = xe
end

function M:removeAll(e)
	self.event[e] = nil
end

function M:emit(e, ...)
	local xe = self.event[e] or {}
	for _, v in ipairs(xe) do
		v(...)
	end
end

function M:eventNames()
	local n = {}
	for k, _ in pairs(self.event) do
		table.insert(n, k)
	end
	return n
end

return M

