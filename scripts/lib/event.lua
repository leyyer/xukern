require("class")
local M = class()

function M:constructor()
	self._event = {}
	self._once = {}
end

function M:once(e, f)
	local xe = self._once[e] or {}
	table.insert(xe, f)
	self._once[e] = xe
end

function M:on(e, f)
	local xe = self._event[e] or {}
	table.insert(xe, f)
	self._event[e] = xe 
end

function M:listenerCount(e)
	local xe = self._event[e] or {}
	local n = 0
	for _, _ in pairs(xe) do
		n = n + 1
	end

	xe = self._once[e]
	if xe ~= nil then
		for _, _ in pairs(xe) do
			n = n + 1
		end
	end
	return n
end

function M:addListener(e, f)
	M:on(e, f)
end


function M:remove(e, f)
	local xe = self._event[e] or {}
	for k, v in ipairs(xe) do
		if v == f then
			table.remove(xe, k)
			break
		end
	end
	self._event[e] = xe

	xe = self._once[e]
	if xe ~= nil then
		for k, v in ipairs(xe) do
			if v == f then
				table.remove(xe, k)
				break
			end
		end
		self._once[e] = xe
	end
end

function M:removeAll(e)
	self._event[e] = nil
	self._once[e] = nil
end

function M:emit(e, ...)
	local xe = self._event[e] or {}
	for _, v in ipairs(xe) do
		v(...)
	end

	xe = self._once[e]
	if xe ~= nil then
		for _, v in ipairs(xe) do
			v(...)
		end
		self._once[e] = nil
	end
end

function M:eventNames()
	local n = {}
	for k, _ in pairs(self._event) do
		table.insert(n, k)
	end
	return n
end

return M

