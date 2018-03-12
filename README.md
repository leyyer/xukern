# xukern

## builtin lua class

### actor class
1. callback(func)       -- set actor's callback function, `func' is the function.
2. name(newname)        -- set actor's name, `newname' is string.
3. query([handle]       -- query actor's name, if [handle] is specified, query its name.
4. timeout(ms, id)      -- add a time handler. `ms' is timeout in microseconds, `id' is the timeout id.
                      A MTYPE_TIMEOUT message emit after `ms' microconds delay.
5. dispatch(dest, src, mtype, msg, sz) -- send a message.
6. launch(actor, param) -- create a new actor, its name is `actor', param to actor's init function.
7. logon(file)          -- active actor's log, save message to `file'
8. logoff()             -- close log file.
9. exit()               -- quit.
10. kill(handle)        -- kill actor `handle'.
11. getenv(env)         -- get env with name `env'.
12. setenv(env, var)    -- set env to `var'
13. now()               -- current time in ms.
14. error(msg)          -- show error msg

