require("btif")

actor.name("btif")

bi, fd = btif.sockOpen(...)

function handle_io(src, msg, sz)
	actor.error("event: " .. ioevent.event(msg) .. " len: " .. ioevent.len(msg))
	if ioevent.event(msg) == 6 then -- data
		bi:put(ioevent.data(msg), ioevent.len(msg))
		bi:power(0)
	end
end

function dispatch(mtype, src, msg, sz)
	actor.error("msgtype: " .. mtype .. " now: " .. actor.now() .. " source: " .. src .. " len: " .. sz)
	if mtype == 3 then  -- IO msg
		handle_io(src, msg, sz)
	end
end

function slip_cmd(cmd, data, sz)
	actor.error("slip cmd " .. cmd .. " len " .. sz)
end

bi:setCallback(slip_cmd)
actor.logon()
actor.error("btif fdesc: " .. tostring(fd))
actor.callback(dispatch)

