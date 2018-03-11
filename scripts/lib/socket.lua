local c = require("core")
local base = require("event")
local S = class(base)

local __conns = {}

function S:constructor(s)
--	print("socket constructor : " .. s)
	self._fd = s 
	__conns[s] = self
end

function S:write(s)
	return sio.write(self._fd, s)
end

function S:close()
	local r =  sio.close(self._fd)
	__conns[self._fd] = nil
	return r
end

function S:fd()
	return self._fd
end

local M = {}

function M.createTcpServer(host, port)
	local s = sio.createTcpServer(host, port)
	return S.new(s)
end

function M.accept(s)
	return S.new(s)
end

local events = {"error", "listen", "connect", "connection", "message", "data", "close", "drain", "peer"}
local function __handle_io(src, msg, sz)
	local fd = ioevent.fd(msg)
	local e = ioevent.event(msg)
	if e == nil then
		actor.error("uncaught socket event type " .. e)
		return
	end
	local c = __conns[fd]
	if c ~= nil then
		local ev = events[e]
		if ev == "connection" then
			local newfd = ioevent.errno(msg)
			c:emit(ev, newfd)
		elseif ev == "data"  or ev == "message" then
			c:emit(ev, ioevent.data(msg), ioevent.len(msg))
		elseif ev == "error" or ev == "drain" then
			c:emit(ev, ioevent.errno(msg))
		elseif ev == "close" then
			c:emit(ev, fd)
		else
			c:emit(ev, msg, sz)
		end
	end
end

function M.init()
	c.register(c.type.MTYPE_IO, __handle_io)
end

return M

