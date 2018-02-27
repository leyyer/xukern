print("hello")
print(".............................")

actor.name("loader")

in6 = actor.address("[::0]", 8000)
print(in6:address(), in6:port(), in6:family())

--in4 = actor.address("192.168.111.128", 8000)
--print(in4:address(), in4:port(), in4:family())

udp = actor.udpOpen()
print("udp: ", udp)

local s = 1
function cb(mtype, source, msg, sz)
	print("msgtype", mtype, "now: ", actor.now(), "source: ", source, "len: ", sz)
	if s == 1 then
		actor.udpSend(udp, in4, "recv msgtype" .. mtype)
		s = 0
	end

	if mtype == 3 then
		print("event ", ioevent.event(msg), "len ", ioevent.len(msg), "errno ", ioevent.errno(msg))
	end

	return 0
--	actor.timeout(100, 1)
end

actor.launch("echo")
actor.callback(cb)
actor.timeout(100, 1)

