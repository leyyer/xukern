local client = Tcp.new("tcp4")

local cnt = 0

local function on_send(status)
	client:send("data status:  " .. tostring(cnt))
	cnt = cnt + 1
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
	else
		client:recvStart(on_recv)
		on_send(0)
	end
end

client:connect("127.0.0.1", 8888, on_connect)

