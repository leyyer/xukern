bif = require("btif")

actor.name("btif")

local arg = { ... }

if arg[1] == "sock" then
	bi, fd = bif.sockOpen(arg[2])
elseif arg[1] == "tty" then
	bi, fd = bif.open(arg[2])
else
	error("can't find type: " .. arg[1])
end

local function ed(msg)
	local ud = ioevent.data(msg)
	local sz = ioevent.len(msg)
	function dump(msg, i)
		if i >= sz then
			return nil
		end
		data = rdbuf.readu8(ud, i)
		i = i + 1
		return i, data
	end
	return dump, msg, 0
end

function handle_io(src, msg, sz)
	actor.error("event: " .. ioevent.event(msg) .. " len: " .. ioevent.len(msg))
	if ioevent.event(msg) == 6 then -- data
		bi:put(ioevent.data(msg), ioevent.len(msg))
		bi:power(0)
		local v = {}
		for _, d in ed(msg) do
			table.insert(v, d)
		end
		print(table.unpack(v))
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

