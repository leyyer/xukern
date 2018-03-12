# xukern

## builtin lua class

### *actor* class
1. *callback(func)*       -- set actor's callback function, `func' is the function.
2. *name(newname)*        -- set actor's name, `newname' is string.
3. *query([handle])*      -- query actor's name, if [handle] is specified, query its name.
4. *timeout(ms, id)*      -- add a time handler. `ms' is timeout in microseconds, `id' is the timeout id.
                      A MTYPE_TIMEOUT message emit after `ms' microconds delay.
5. *dispatch(dest, src, mtype, msg, sz)* -- send a message.
6. *launch(actor, param)* -- create a new actor, its name is `actor', param to actor's init function.
7. *logon(file)*          -- active actor's log, save message to `file'
8. *logoff()*             -- close log file.
9. *exit()*               -- quit.
10. *kill(handle)*        -- kill actor `handle'.
11. *getenv(env)*         -- get env with name `env'.
12. *setenv(env, var)*    -- set env to `var'
13. *now()*               -- current time in ms.
14. *error(msg)*          -- show error msg

### *sio* class
1. *createTcpServer(host, port)*    -- create tcp server socket, return a `fd`. 
2. *createUdpServer(host, port)*    -- create udp server socket, return a `fd`.
3. *close(fd)* -- close socket `fd`.
4. *connect(host, port)* -- connect a remote server.
5. *write(fd, data, [len])* -- write data to socket `fd`, data may be a string or userdata type.
6. *udpOpen("udp4" | "udp6")* -- create a udp4 or udp6 socket.
7. *udpSend(fd, address, string | userdata, [len])* -- send udp message to address.
8. *addMembership(fd, multicast, local_interface)* -- add local_interface to multicast group
9. *dropMembership(fd, multicast, local_interface)* -- drop a membership.
10. *setMulticastLoopback(fd, enable)*
11. *setBroadcast(fd, enable)*
12. *setKeepalive(fd, enable)*
13. *udpPeer(userdata)* -- convert userdata to address object.
14. *address(ip, port)* -- create a address object.

### *address* class
1. *:family()*  -- return address's family, "ipv4" or "ipv6".
2. *:address()* -- return ip address string.
3. *:port()* --  return address's port.


