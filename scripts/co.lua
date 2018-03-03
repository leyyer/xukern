actor.name("co")
server = sio.createTcpServer("0.0.0.0", 61000)

function handle_io(msg, sz)
	return 0
end

function printmsg(msgtype, src, msg, sz)
	actor.error("printmsg running")
	while true do
		msgtype, src, msg, sz = coroutine.yield()
		actor.error("msgtype: " .. msgtype .. " now: " .. actor.now() .. " source: " .. src .. " len: " .. sz)
	end
end

local co = coroutine.create(printmsg)

function dispatch(msgtype, src, msg, sz)
	coroutine.resume(co, msgtype, src, msg, sz)
--	actor.error("msgtype: " .. msgtype .. " now: " .. actor.now() .. " source: " .. src .. " len: " .. sz)
--	if msgtype == 3 then -- IO event
--		return handle_io(msg, sz)
--	end
end

coroutine.resume(co)
actor.callback(dispatch)
print("co running")

