MODS := logger.so 

LDFLAGS += -L../ -lxucore

all: $(MODS)

xalua.so : xulua.c lua_tcp.c
	$(CC) $(CFLAGS) $(LDFLAGS) -fPIC -shared $^ -o $@ 

logger.so : logger.c
	$(CC) $(CFLAGS) $(LDFLAGS) -fPIC -shared $^ -o $@ 
console.so : console.c
	$(CC) $(CFLAGS) $(LDFLAGS) -fPIC -shared $^ -o $@ 
