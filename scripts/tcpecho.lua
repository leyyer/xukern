actor.name("tcp")
server = sio.createTcpServer("0.0.0.0", 61000)

local clients = {}

function process(fd, s)
	print("command line: ")
	-- remove '\b'
	if s == "query" then
		actor.write(fd, actor.query() .. "\r\n")
	end
	print("------------------------")
end

function handle_io(msg, sz)
	local event = ioevent.event(msg)
	local fd = ioevent.fd(msg)
	actor.error("event: " .. event .. " fd: " .. fd)
	if event == 4 then
		local fd = ioevent.fd(msg)
		clients[fd] = {}
		actor.write(fd, "\xFF\xFB\x03\xFF\xFB\x01\xFF\xFD\x03\xFF\xFD\x01> ")
	end
	if event == 6 then
		local ud = ioevent.data(msg)
		local sz = ioevent.len(msg)
		local data = clients[fd]
		local r = {}
		for i=1,sz do
			local ch = rdbuf.readu8(ud, i - 1)
			if ch == string.byte('\b') then
				table.insert(r, "\b \b")
				table.insert(data, '\b')
			elseif ch == string.byte('\n') then
				table.insert(r, "\n> ")
				process(fd, table.concat(data))
				clients[fd] = {}
			elseif ch == string.byte('\r') then
				print("carrier")
				process(fd, table.concat(data))
				clients[fd] = {}
			else 
				table.insert(r, string.char(ch))
				table.insert(data, string.char(ch))
			end
		end
		local sd = table.concat(r)
		actor.write(fd, sd);
--		actor.error(s)
--		print(table.concat(data))
	end
	if event == 8 then
		errno = ioevent.errno(msg)
		actor.error("drain " .. errno)
	end
	return 0
end

function dispatch(msgtype, src, msg, sz)
	actor.error("msgtype: " .. msgtype .. " now: " .. actor.now() .. " source: " .. src .. " len: " .. sz)
	if msgtype == 3 then -- IO event
		return handle_io(msg, sz)
	end
	return 0
end

actor.callback(dispatch)
actor.error("threads: " .. os.getenv("UV_THREADPOOL_SIZE"))
