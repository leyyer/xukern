local args = {}
for word in string.gmatch(..., "%S+") do
	table.insert(args, word)
end

local p = actor.getenv("lua_path")
if p then
	package.path = p
else
	package.path = "./scripts/?.lua"
end

p = actor.getenv("lua_cpath")
if p then
	package.cpath = p
end

local f, msg

local err = {}
for pat in string.gmatch(package.path, "([^;]+);*") do
	local filename = string.gsub(pat, "?", args[1])
	f, msg = loadfile(filename)
	if not f then
		table.insert(err, msg)
	else
		break
	end
end

if not f then
	error(table.concat(err, "\n"))
end

actor.name(args[1])

f(select(2, unpack(args)))

