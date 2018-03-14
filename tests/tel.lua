local tel = require("telnet")

local args = { ... }
local socket = require("socket")
local s = socket.createTcpServer("0.0.0.0", args[1])

function on_connection(fd)
	local c = socket.accept(fd)
	local con = tel.new()
	c:on("data", function(d, len) con:recv(d, len) end)
	c:on("close", function() c:close() con:close() end)
	con:setCallback(function(e, s)
		if e == "data" then
			print("recv:", s)
			con:send(s)
		elseif e == "send" then
			print("send", s)
			c:write(s)
		end
	end)
end

s:on("connection", on_connection)

core = require("core")
core.entry()

