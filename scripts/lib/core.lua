local protos = {}

local M = {
	type = {
		MTYPE_TIMEOUT = 1,
		MTYPE_LOG = 2,
		MTYPE_IO = 3
	}
}

function M.register(p, f)
	protos[p] = f
end

local function __dispatch_msg(mtype, src, msg, sz)
--	actor.error("msgtype: " .. mtype .. " now: " .. actor.now() .. " source: " .. src .. " len: " .. sz)
	for k, f in pairs(protos) do
		if k == mtype then
			if f ~= nil then
				f(src, msg, sz)
			end
			break
		end
	end
end

function M.call()
	local so = require("socket")
	so.init()
	actor.callback(__dispatch_msg)
end

return M

