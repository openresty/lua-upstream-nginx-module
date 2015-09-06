local log = ngx.log
local ERR = ngx.ERR
local INFO = ngx.INFO
local WARN = ngx.WARN
local DEBUG = ngx.DEBUG
local str_find = string.find
local sub = string.sub
local str_len = string.len
local gsub = string.gsub
local table_sort = table.sort
local new_timer = ngx.timer.at
local shared = ngx.shared
local debug_mode = ngx.config.debug
local worker_pid = ngx.worker.pid()
local tonumber = tonumber
local pairs = pairs


local _M = {
    _VERSION = '0.01'
}


local ok, upstream = pcall(require, "ngx.upstream")
if not ok then
    error("ngx_upstream_lua module required")
end


local set_peer_down = upstream.set_peer_down
local add_server = upstream.add_server
local add_peer = upstream.add_peer
local remove_server = upstream.remove_server
local remove_peer = upstream.remove_peer
local get_primary_peers = upstream.get_primary_peers
local get_backup_peers = upstream.get_backup_peers
local get_upstreams = upstream.get_upstreams


local function addserver(upstream, ipport)
    local ok,err = add_server(upstream, ipport, server_info.weight, server_info.max_fails, server_info.fail_timeout)
    return ok, err
end


-- global var is save some server general information
-- forexample weight max_fails fail_timeout 
server_info = { }
server_info.weight = 2
server_info.max_fails = 10
server_info.fail_timeout = 10

-- action save some function addrs that make it dynamicly running
local action = { }
action.add_server = addserver
action.add_peer = add_peer
action.remove_server = remove_server
action.remove_peer = remove_peer


local function info(...)
    log(INFO, "dyupsc: ", ...)
end


local function warn(...)
    log(WARN, "dyupsc: ", ...)
end


local function errlog(...)
    log(ERR, "dyupsc: ", ...)
end


local function debug(...)
    if debug_mode then
        log(DEBUG, "dyupsc: ", ...)
    end
end


local function trim (s)
  return gsub(s, "^%s*(.-)%s*$", "%1")
end 
 

local function split(buf, sep)  
    local findstartindex = 1   
    local splitindex = 1   
    local splitarray = { }  
    while true do  
        local findlastindex = str_find(buf, sep, findstartindex)  
        if not findlastindex then  
           splitarray[splitindex] = sub(buf, findstartindex, str_len(buf))  
           break  
        end  
        splitarray[splitindex] = sub(buf, findstartindex, findlastindex - 1)  
        findstartindex = findlastindex + str_len(sep)  
        splitindex = splitindex + 1   
    end  
    return splitarray  
end


local function run_lock(cmdkey, ctx)
    local dict = ctx.dict
    local cmd = split(cmdkey, ":")
    local key = worker_pid..":"..cmdkey
    if  tonumber(cmd[1]) then 
        key = cmdkey
    end

    local ok, err = dict:add(key, true, ctx.interval*2)
    if not ok then
        if err == "exists" then
            return nil
        end
        errlog("failed to add key \"", key, "\": ", err)
        return nil
    end
    return true
end


local function dec_sort(table) 
    table_sort(table,function(a,b) return a>b end)
end


local function do_run(ctx)
    local dict = ctx.dict
    local worker_process = ctx.worker_process
    local listkeys = dict:get_keys(0)
    if #listkeys <= 0 then 
        info("the cmd list keys is null")
        return
    end
	
    local key = "version"
    local ver, err = dict:get(key)
    if not ver then
	errlog("failed to get cmd version that not \"version\" key")
        return
    end

    if err then
	errlog("failed to get cmd version: ", err)
        return
    end
    -- make listkeys sort and make add_server before add_peer running 
    -- because add_peer will judge whether the server
    dec_sort(listkeys)

    for i, v in pairs(listkeys) do
        local cmd = split(v,":")
        if (v ~= "version") and run_lock(v, ctx)  and (ctx.version <= ver) then
            if err then
                errlog("get listkes value error: ", err)
                return
            end  
   
            local num, err = dict:get(v)
            if not num  then 
                errlog("do not get cmd value")
                return
            end

            if err  then 
                errlog("fail get cmd value",err)
                return
            end

            if num >= worker_process then 
                dict:delete(v)

            else 
                dict:incr(v, 1)    
                local operation = trim(cmd[1])
                local upstream = trim(cmd[2])
                local ip = trim(cmd[3])
                local port = trim(cmd[4]) 

                if not operation  or not upstream or not ip or not port then
                    errlog("may be operation or upstream or ip or port is nil")
                    return
                end              

                local ok, err = action[operation](upstream, ip..":"..port)
                if not ok then
		    errlog(operation," is fail error info: ",err)
                    return
                end

            end
        end
    end
    ctx.version = ver
end    


local check_pull
check_pull = function (premature, ctx)
    if premature then
        return
    end

    local ok, err = pcall(do_run, ctx)
    if not ok then
        errlog("failed to run dyupsc cycle: ", err)
    end

    local ok, err = new_timer(ctx.interval, check_pull, ctx)
    if not ok then
        if err ~= "process exiting" then
            errlog("failed to create timer: ", err)
        end
        return
    end
end


function _M.dyups_checker(opts)
    local interval = opts.interval
    if not interval then
        interval = 5 

    else
        interval = interval / 1000
        if interval < 0.002 then  -- minimum 2ms
            interval = 0.002
        end
    end 

    local worker_process = opts.worker_process
    if not worker_process then 
        worker_process = 2
    end
    
    -- debug("interval: ", interval)
    local shm = opts.shm
    if not shm then
        return nil, "\"shm\" option required"
    end

    local dict = shared[shm]
    if not dict then
        return nil, "shm \"" .. tostring(shm) .. "\" not found"
    end

    server_info.weight = opts.weight
    server_info.max_fails = opts.max_fails
    server_info.fail_timeout = opts.fail_timeout
    
    local ctx = {
        interval = interval,
        dict = dict,
        version = 0,
        worker_process = worker_process,
    }
     
    local ok, err = new_timer(0, check_pull, ctx)
    if not ok then
        return nil, "failed to create timer: " .. err
    end

    return true
end


return _M
