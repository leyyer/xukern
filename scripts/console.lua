socket = require("socket")
actor.name("console")
local MTYPE_IO = 3
local args = { ... }
local events = {"error", "listen", "connect", "connection", "message", "data", "close", "drain"}
local e = ioevent

server = socket.createTcpServer("0.0.0.0", args[1])

conns = {}

function sockin(fd, buf, sz)
	return function(buf, i)
		if i == sz then
			return nil
		end
		local d = rdbuf.readu8(buf, i)
		i = i + 1
		return i, d
	end, buf, 0
end

function string:split(sep)
	local sep, fields = sep or "\t", {}
	local p = string.format("[^%s]+", sep)
	self:gsub(p, function(c) fields[#fields + 1] = c end)
	return fields
end

function handle_cmd(c, line)
	fields = line:split("\t ")
	if fields[1] == "quit" or fields[1] == "exit" then
		c:close()
	elseif fields[1] == "launch" then
		actor.launch(fields[2], table.concat(fields, " ", 3))
	elseif fields[1] == "kill" and fields[2] ~= nil then
		actor.kill(fields[2])
	else 
		c:write("unknown: " .. fields[1] .. "\r\n")
	end
end

function put_data(c, buf, sz)
	local fd = c:fd()
	local state = conns[fd] or {}
	local d = state.data or ""
	local o = state.option or 0
	local ob = ""
	local cend = false

	for _, dt in sockin(fd, buf, sz) do
		if dt == 0xff and o == 0 then
			o = o + 1
		elseif o ~= 0 then
			if dt >= 0xfb and dt <= 0xfe then
				o = dt 
			elseif dt ~= 0xff then 
				o = 0
			else
				o = 0
			end
		elseif dt == 0x8 then -- '\b'
			local slen = d:len()
			if slen > 0 then
				d = d:sub(1, slen - 1)
				ob = ob .. '\b \b'
			end
		elseif dt == 0xa then -- '\n'
		elseif dt == 0xd then -- '\r'
			ob = ob .. "\r\n"
			cend = true
			break
		else
			ob = ob .. string.char(dt)
			d = d .. string.char(dt)
		end
	end
	state.option = o
	state.data = d
	conns[fd] = state
	c:write(ob);
	if cend then
		handle_cmd(c, d)
		state.data = ""
		c:write("> ")
	end
end

local function on_data(c, msg, len)
	return function(msg, len) put_data(c, msg, len) end
end

server:on("connection", function(fd)
		local c = socket.accept(fd)
		c:on("data", on_data(c, msg, len))
		c:on("close", function() c:close() conns[c:fd()] = nil end)
		conns[fd] = {}
		c:write("\xFF\xFB\x03\xFF\xFB\x01\xFF\xFD\x03\xFF\xFD\x01> ")
end)

local c = require("core")
c.call()
actor.logon()
actor.error("console bind port: " .. args[1])

