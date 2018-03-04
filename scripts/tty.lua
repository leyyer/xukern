require("btif")

actor.name("btif")

bi, fd = btif.open(...)

function handle_io(src, msg, sz)
	actor.error("event: " .. ioevent.event(msg) .. " len: " .. ioevent.len(msg) .. " data: " .. ioevent.tostring(msg))
	if ioevent.event(msg) == 6 then -- data
		bi:put(ioevent.data(msg), ioevent.len(msg))
		bi:step()
		bi:power(0)
		bi:power(1)
--		bi:reboot()
	end
end

function dispatch(mtype, src, msg, sz)
	actor.error("msgtype: " .. mtype .. " now: " .. actor.now() .. " source: " .. src .. " len: " .. sz)
	if mtype == 3 then  -- IO msg
		handle_io(src, msg, sz)
	end
end

actor.logon("tty.log.txt")
actor.error("btif fdesc: " .. tostring(fd))
actor.callback(dispatch)

