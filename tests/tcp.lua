local server = Tcp.new("tcp4")
local r = server:bind(8888)
print("bind: " .. tostring(r))

function new_client(status, c)
	print "new client in"
	function on_read(n, buf)
		print(" recv " .. n .. " ..bytes")
		if n ~= 0 then
			print(buf:toString())
			c:send(buf)
		else
			c:close()
		end
	end

	if status then
		c:recvStart(on_read)
	end
end

server:listen(10, new_client)

