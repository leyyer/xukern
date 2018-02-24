server = Tcp.new("tcp4")
local r = server:bind(8888)
print("bind: " .. tostring(r))

print(server)

function on_send(status)
	print("status" .. status)
end

function new_client(status, c)
	print "new client in"
	print(c)
	c:onSend(on_send)
	function on_read(n, buf)
--		print(" recv " .. n .. " ..bytes")
		if n > 0 then
			print(buf:toString())
			print(c:send(buf))
		else
			c:close()
		end
	end

	if status then
		c:recvStart(on_read)
	end
end

server:listen(10, new_client)

