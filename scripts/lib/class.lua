local __class = {}
function class(super)
	local class_type = {}
	class_type.constructor = false
	class_type.super = super
	class_type.new = function (...)
		local obj = {}
		do
			local f
			f = function(c, ...)
				if c.super then
					f(c.super, ...)
				end
				if c.constructor then
					c.constructor(obj, ...)
				end
			end
			f(class_type, ...)
		end
		setmetatable(obj, {__index = __class[class_type]})
		return obj
	end
	local methods = {}
	__class[class_type] = methods
	setmetatable(class_type, {__newindex = function(t, k, v)
		methods[k] = v
	end})
	if super then
		setmetatable(methods, {__index = function(t, k)
			local r = __class[super][k]
			methods[k] = r
			return r
		end})
	end
	return class_type
end

