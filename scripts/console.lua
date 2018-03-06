actor.name("console")
local MTYPE_IO = 3
local args = { ... }
local events = {"error", "listen", "connect", "connection", "message", "data", "close", "drain"}
local e = ioevent

server = sio.createTcpServer("0.0.0.0", args[1])

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

function handle_cmd(fd, line)
	fields = line:split("\t ")
--[[
	for i, v in ipairs(fields) do
		print(i, v)
	end
--]]
	if fields[1] == "quit" or fields[1] == "exit" then
		sio.close(fd)
	elseif fields[1] == "launch" then
		actor.launch(fields[2], table.concat(fields, " ", 3))
	elseif fields[1] == "kill" then
		actor.kill(fields[2])
	end
end

function put_data(fd, buf, sz)
	local state = conns[fd] or {}
	local d = state["data"] or {}
	local o = state["option"] or 0
	local ob = {}
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
		elseif dt == 0xa then -- '\n'
		elseif dt == 0xd then -- '\r'
			table.insert(ob, "\r\n")
			cend = true
			break
		else
			table.insert(ob, string.char(dt))
			table.insert(d, string.char(dt))
		end
	end
	state["option"] = o
	state["data"] = d
	conns[fd] = state
	sio.write(fd, table.concat(ob))
	if cend then
		handle_cmd(fd, table.concat(d))
		state["data"] = nil
		sio.write(fd, "> ")
	end
end

function handle_io(src, msg, sz)
	local et = e.event(msg)
	local fd = e.fd(msg)
	actor.error("event type: " .. events[et] .. " fd " .. fd .. " msglen " .. e.len(msg))
	if et == 4 then -- new connection
		conns[fd] = {}
		sio.write(fd, "\xFF\xFB\x03\xFF\xFB\x01\xFF\xFD\x03\xFF\xFD\x01> ")
	elseif et == 7 then
		conns[fd] = nil
	elseif et == 6 then
		put_data(fd, e.data(msg), e.len(msg))
	end
end

function dispatch(msgtype, src, msg, sz)
	actor.error("msgtype: " .. msgtype .. " now: " .. actor.now() .. " source: " .. src .. " len: " .. sz)
	if msgtype == MTYPE_IO then
		handle_io(src, msg, sz)
	end
end

actor.callback(dispatch)
actor.logon()
actor.error("console bind port: " .. args[1])
