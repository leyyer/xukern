MODS := telnet.so btif.so

LDFLAGS += -L../ -Wl,-rpath,.

TELNET_SRCS := telnet/libtelnet.c \
	telnet/lua_telnet.c

BTIF_SRCS := btif/slip.c \
	btif/btif.c \
	btif/lua_btif.c \

all: $(MODS)

telnet.so : $(TELNET_SRCS)
	$(CC) $(CFLAGS) $(LDFLAGS) -fPIC -shared $^ -o $@ 

btif.so : $(BTIF_SRCS)
	$(CC) $(CFLAGS) $(LDFLAGS) -fPIC -shared $^ -o $@ 
