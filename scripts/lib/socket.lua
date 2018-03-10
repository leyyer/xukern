local base = require("event")
local S = class(base)

function S:constructor(fd)
	self._fd = fd
end

function S:write(s)
	return sio.write(self._fd, s)
end

function S:close()
	return sio.close(self._fd)
end

function S:fd()
	return self._fd
end

local M = {}

function M.createTcpServer(host, port)
	local fd = sio.createTcpServer(host, port)
	return S.new(fd)
end

function M.accept(fd)
	return S.new(fd)
end

return M
