xucore.println ("hello world")

local create = coroutine.create
local yield = coroutine.yield
local resume = coroutine.resume

local b = Buffer.alloc(256)
print(b)
print(b:len())
b:writeInt8(100, 0xff)
print(b:readInt8(0xff))
print("len " .. b:len())
--print("toString " .. b:toString())
b:fill(65)
--print("toString2 " .. b:toString())
local s1 = b:toString(200)
local s2 = b:toString(200, 300)
local s3 = b:toString(200, 20)

--[[
print(s1)
print(s2)
print(s3)
--]]

print("----------------------------")

b:dump(100)
print("***************")
b:dump(100, 10)

--[[
function echo()
	local udp = Udp.new("udp4")
	local co

	function wakeup(s, b)
		resume(co, s, b)
		b = nil
	end

	function recv()
		while true do
			local r, b = yield(co)
			print ("recv yield return")
			print(r, ':', b)
			local p = udp:peer()
			print("peer " .. p)
			udp:sendWithBuffer(p:address(), p:port(), pb)
		end
	end
	co = create(recv)
	udp:bind(8888)
	udp:recvStart(wakeup)
	resume(co)
	local adr = udp:address()
	print("udp address: " .. adr:address() .. " " .. adr:port());
	adr = nil
end

echo()
print ("done")
--]]

--[--[
local udp = Udp.new("udp4")
print(udp:bind("192.168.111.128", 8888))
function recv(s, b, peer)
	print("recv <".. s .. "> ")
	print("peer address " .. peer:address() .. ':' .. peer:port())
	print(b)
	if b ~= nil then
		print(b:toString())
	end
	print(udp:send(peer:address(), peer:port(), b))
--[[	peer = nil
	b = nil
	print("before collect: " .. collectgarbage("count"))
	collectgarbage("collect")
	print("after collect: " .. collectgarbage("count"))
--]]
end

print(udp:recvStart(recv))
--]--]

