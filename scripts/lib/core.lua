local __protos = {}

local M = {
	type = {
		MTYPE_TIMEOUT = 1,
		MTYPE_LOG = 2,
		MTYPE_IO = 3
	}
}

function M.register(p, f)
	__protos[p] = f
end

local function __dispatch_msg(mtype, src, msg, sz)
--	actor.error("msgtype: " .. mtype .. " now: " .. actor.now() .. " source: " .. src .. " len: " .. sz)
	local f = __protos[mtype]
	if f ~= nil then
		f(src, msg, sz)
	end
end

function M.entry()
	local so = require("socket")
	local tm = require("timeout")
	so.init()
	tm.init()
	actor.callback(__dispatch_msg)
end

function string:split(sep)
	local sep, fields = sep or "\t", {}
	local p = string.format("[^%s]+", sep)
	self:gsub(p, function(c) fields[#fields + 1] = c end)
	return fields
end

return M

