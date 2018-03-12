require("class")
local T = class()
local M = {idx = 1, timers = {}}

function T:constructor(p, f, ms, arg)
	local i = M.idx
	M.idx = i + 1
	self.session = i
	self.delay = ms
	self.periodic = p
	self.callback = f
	self.params = arg
	actor.timeout(self.delay, i)
	M.timers[i] = self
end

local function __do_timeout(src, msg, len)
	if len ~= 0 then
		actor.error("Maybe a bug: not a timeout message")
	else
		local tm = M.timers[msg]
		if tm ~= nil then
			f = tm.callback
			f(table.unpack(tm.params))
			if tm.periodic then
				actor.timeout(tm.delay, msg)
			else
				M.timers[msg] = nil
			end
		end
	end
end

function T:setTimeout(ms)
	self.delay = ms
end

function M.timeout(f, ms, ...)
	if f == nil then
		print("BUG: please specify callback function")
		return
	end
	local args = { ... }
	return T.new(false, f, ms, args)
end

function M.interval(f, ms, ...)
	if f == nil then
		print("BUG: please specify callback function")
		return
	end
	local args = { ... }
	return T.new(true, f, ms, args)
end

function M.init()
	local c = require("core")
	c.register(c.type.MTYPE_TIMEOUT, __do_timeout)
end

return M
