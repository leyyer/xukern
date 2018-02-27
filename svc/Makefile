MODS := logger.so  echo.so xulua.so

LDFLAGS += -L../ -lxucore

all: $(MODS)

xalua.so : xulua.c lua_tcp.c
	$(CC) $(CFLAGS) $(LDFLAGS) -fPIC -shared $^ -o $@ 

logger.so : logger.c
	$(CC) $(CFLAGS) $(LDFLAGS) -fPIC -shared $^ -o $@ 
console.so : console.c
	$(CC) $(CFLAGS) $(LDFLAGS) -fPIC -shared $^ -o $@ 
echo.so : echo.c
	$(CC) $(CFLAGS) $(LDFLAGS) -fPIC -shared $^ -o $@ 
xulua.so : xulua.c
	$(CC) $(CFLAGS) $(LDFLAGS) -fPIC -shared $^ -o $@ 
