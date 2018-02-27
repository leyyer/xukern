actor.name("tcp")
server = actor.createTcpServer("0.0.0.0", 61000)

local clients = {}

function handle_io(msg, sz)
	local event = ioevent.event(msg)
	local fd = ioevent.fd(msg)
	actor.error("event: " .. event .. " fd: " .. fd)
	if event == 4 then
		local fd = ioevent.fd(msg)
		clients[fd] = {recv = ""}
	end
	if event == 6 then
		local data = clients[fd]
--		data[recv] = data[recv] .. ioevent.tostring(msg)
		--print("data: " .. data[recv])
		actor.write(fd, ioevent.tostring(msg))
		actor.error(ioevent.tostring(msg))
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
