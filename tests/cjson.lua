cjson = require("cjson")
js = cjson.new()
value = js.decode('{"foo" : "bar"}')

for k, v in pairs(value) do
	print(k, v)
end

