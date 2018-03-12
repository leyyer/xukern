socket = require("socket")
actor.name("console")
local args = { ... }

server = socket.createTcpServer("0.0.0.0", args[1])

function sockin(buf, sz)
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
	local con = c.con
	fields = line:split("\t ")
	if fields[1] == "quit" or fields[1] == "exit" then
		con:close()
		c.con = nil
	elseif fields[1] == "launch" then
		actor.launch(fields[2], table.concat(fields, " ", 3))
	elseif fields[1] == "kill" and fields[2] ~= nil then
		actor.kill(fields[2])
	else 
		con:write("unknown: " .. fields[1] .. "\r\n")
	end
end

function put_data(c, buf, sz)
	local d = c.data or ""
	local o = c.option or 0
	local ob = ""
	local cend = false

	for _, dt in sockin(buf, sz) do
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
	c.option = o
	c.data = d
	local con = c.con
	con:write(ob);
	if cend then
		handle_cmd(c, d)
		c.data = ""
		con:write("> ")
	end
end

server:on("connection", function(fd)
		local c = socket.accept(fd)
		local client = {con = c, data = "", option = 0}
		c:on("data", function(msg, len) put_data(client, msg, len) end)
		c:on("close", function() c:close() client = nil end)
		c:write("\xFF\xFB\x03\xFF\xFB\x01\xFF\xFD\x03\xFF\xFD\x01> ")
end)

--local tm = require("timeout")
--tm.interval(function(a, b, c) actor.error("now: " .. actor.now() .. " timeout .. : " .. a .. " " .. b .. " " .. c .. " ") end, 20, 99,  88, 77)
actor.logon()
actor.error("console bind port: " .. args[1])
local c = require("core")
c.entry()
