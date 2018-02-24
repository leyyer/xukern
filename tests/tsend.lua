local client = Tcp.new("tcp4")

local tm = Timer.new()

local cnt = 0

local function on_timer()
	client:send("count [ " .. tostring(cnt) ..  " ]\n")
	cnt = cnt + 1
	tm:stop()
end

local function on_send(status)
	tm:restart()
end

client:onSend(on_send)

local function on_recv(n, b)
	if b ~= nil then
		print("recv: " .. b:toString())
	else
		print("recv: " .. n .. " bytes")
	end
end

local function on_connect(status)
	if status ~= 0 then
		print("connect failed")
		client:close()
	else
		client:recvStart(on_recv)
		tm:start(on_timer, 10, 10)
	end
end

client:connect("127.0.0.1", 8888, on_connect)

