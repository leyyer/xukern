local udp = Udp.new("udp4")

local co = nil
local function recv(n, b, addr)
	print(b)
	print("recv: " .. b:toString())
	coroutine.resume(co, 0)
end

local function send()
	local i = 0
	while true do
		udp:send("192.168.111.128", 8888, "hello " .. i)
		i = i + 1
		print("times " .. i)
		r = coroutine.yield(co)
		print(r)
	end
end

co = coroutine.create(send)
local function onerror(status)
	xucore.println("onerror " .. status)
	coroutine.resume(co, status)
end

print(udp:recvStart(recv))
--print(udp:onSend(onerror))

local addr = udp:address()
print("local: " .. addr:address() .. ":" .. addr:port())

coroutine.resume(co)

