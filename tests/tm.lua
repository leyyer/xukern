tm = Timer.newTimerfd()
--tm = Timer.new()

function on_timer()
	print(xucore.now())
end

tm:start(on_timer, 10, 10)

