# vim:set ft= ts=4 sw=4 et fdm=marker:

use Test::Nginx::Socket::Lua;

#worker_connections(1014);
#master_on();
#workers(2);
#log_level('warn');

repeat_each(1);

plan tests => repeat_each() * (blocks() * 3);

$ENV{TEST_NGINX_MEMCACHED_PORT} ||= 11211;

$ENV{TEST_NGINX_MY_INIT_CONFIG} = <<_EOC_;
lua_package_path "t/lib/?.lua;;";
_EOC_

#no_diff();
no_long_string();
run_tests();

__DATA__

=== TEST 1: get upstream names
--- http_config
    upstream foo.com:1234 {
        server 127.0.0.1;
    }

    upstream bar {
        server 127.0.0.2;
    }
--- config
    location /t {
        content_by_lua '
            local upstream = require "ngx.upstream"
            local us = upstream.get_upstreams()
            for _, u in ipairs(us) do
                ngx.say(u)
            end
            ngx.say("done")
        ';
    }
--- request
    GET /t
--- response_body
foo.com:1234
bar
done
--- no_error_log
[error]



=== TEST 2: get upstream names (no upstream)
--- http_config
--- config
    location /t {
        content_by_lua '
            local upstream = require "ngx.upstream"
            local us = upstream.get_upstreams()
            for _, u in ipairs(us) do
                ngx.say(u)
            end
            ngx.say("done")
        ';
    }
--- request
    GET /t
--- response_body
done
--- no_error_log
[error]



=== TEST 3: get servers
--- http_config
    $TEST_NGINX_MY_INIT_CONFIG

    upstream foo.com:1234 {
        server 127.0.0.1 fail_timeout=53 weight=4 max_fails=100;
        server agentzh.org:81 backup;
    }

    upstream bar {
        server 127.0.0.2;
    }
--- config
    location /t {
        content_by_lua '
            local upstream = require "ngx.upstream"
            local ljson = require "ljson"
            for _, host in pairs{ "foo.com:1234", "bar", "blah" } do
                local srvs, err = upstream.get_servers(host)
                if not srvs then
                    ngx.say("failed to get servers: ", err)
                else
                    ngx.say(host, ": ", ljson.encode(srvs))
                end
            end
        ';
    }
--- request
    GET /t
--- response_body
foo.com:1234: [{"addr":"127.0.0.1:80","fail_timeout":53,"max_fails":100,"weight":4},{"addr":"106.187.41.147:81","backup":true,"fail_timeout":10,"max_fails":1,"weight":1}]
bar: [{"addr":"127.0.0.2:80","fail_timeout":10,"max_fails":1,"weight":1}]
failed to get servers: upstream not found

--- no_error_log
[error]



=== TEST 4: sample in README
--- http_config
    upstream foo.com {
        server 127.0.0.1 fail_timeout=53 weight=4 max_fails=100;
        server agentzh.org:81;
    }

    upstream bar {
        server 127.0.0.2;
    }

--- config
    location = /upstreams {
        default_type text/plain;
        content_by_lua '
            local concat = table.concat
            local upstream = require "ngx.upstream"
            local get_servers = upstream.get_servers
            local get_upstreams = upstream.get_upstreams

            local us = get_upstreams()
            for _, u in ipairs(us) do
                ngx.say("upstream ", u, ":")
                local srvs, err = get_servers(u)
                if not srvs then
                    ngx.say("failed to get servers in upstream ", u)
                else
                    for _, srv in ipairs(srvs) do
                        local first = true
                        for k, v in pairs(srv) do
                            if first then
                                first = false
                                ngx.print("    ")
                            else
                                ngx.print(", ")
                            end
                            if type(v) == "table" then
                                ngx.print(k, " = {", concat(v, ", "), "}")
                            else
                                ngx.print(k, " = ", v)
                            end
                        end
                        ngx.print("\\n")
                    end
                end
            end
        ';
    }
--- request
    GET /upstreams
--- response_body
upstream foo.com:
    addr = 127.0.0.1:80, weight = 4, fail_timeout = 53, max_fails = 100
    addr = 106.187.41.147:81, weight = 1, fail_timeout = 10, max_fails = 1
upstream bar:
    addr = 127.0.0.2:80, weight = 1, fail_timeout = 10, max_fails = 1
--- no_error_log
[error]



=== TEST 5: multi-peer servers
--- http_config
    $TEST_NGINX_MY_INIT_CONFIG
    upstream test {
        server multi-ip-test.openresty.com;
    }
--- config
    location /t {
        content_by_lua '
            local upstream = require "ngx.upstream"
            local ljson = require "ljson"
            local srvs, err = upstream.get_servers("test")
            if not srvs then
                ngx.say("failed to get test ", err)
                return
            end
            ngx.say(ljson.encode(srvs))
        ';
    }
--- request
    GET /t
--- response_body_like chop
^\[\{"addr":\["\d{1,3}(?:\.\d{1,3}){3}:80"(?:,"\d{1,3}(?:\.\d{1,3}){3}:80")+\],"fail_timeout":10,"max_fails":1,"weight":1\}\]$

--- no_error_log
[error]



=== TEST 6: get primary peers: multi-peer servers
--- http_config
    $TEST_NGINX_MY_INIT_CONFIG
    upstream test {
        server multi-ip-test.openresty.com;
    }
--- config
    location /t {
        content_by_lua '
            local upstream = require "ngx.upstream"
            local ljson = require "ljson"
            local peers, err = upstream.get_primary_peers("test")
            if not peers then
                ngx.say("failed to get primary peers: ", err)
                return
            end
            ngx.say(ljson.encode(peers))
        ';
    }
--- request
    GET /t
--- response_body_like chop
^\[\{"current_weight":0,"effective_weight":1,"fail_timeout":10,"fails":0,"id":0,"max_fails":1,"name":"\d{1,3}(?:\.\d{1,3}){3}:80","weight":1\}(?:,\{"current_weight":0,"effective_weight":1,"fail_timeout":10,"fails":0,"id":\d+,"max_fails":1,"name":"\d{1,3}(?:\.\d{1,3}){3}:80","weight":1\})+\]$

--- no_error_log
[error]



=== TEST 7: get primary peers
--- http_config
    $TEST_NGINX_MY_INIT_CONFIG
    upstream foo.com:1234 {
        server 127.0.0.6 fail_timeout=5 backup;
        server 127.0.0.1 fail_timeout=53 weight=4 max_fails=100;
        server agentzh.org:81;
    }

    upstream bar {
        server 127.0.0.2;
        server 127.0.0.3 backup;
        server 127.0.0.4 fail_timeout=23 weight=7 max_fails=200 backup;
    }
--- config
    location /t {
        content_by_lua '
            local upstream = require "ngx.upstream"
            local ljson = require "ljson"
            us = upstream.get_upstreams()
            for _, u in ipairs(us) do
                local peers, err = upstream.get_primary_peers(u)
                if not peers then
                    ngx.say("failed to get peers: ", err)
                    return
                end
                ngx.say(ljson.encode(peers))
            end
        ';
    }
--- request
    GET /t
--- response_body
[{"current_weight":0,"effective_weight":4,"fail_timeout":53,"fails":0,"id":0,"max_fails":100,"name":"127.0.0.1:80","weight":4},{"current_weight":0,"effective_weight":1,"fail_timeout":10,"fails":0,"id":1,"max_fails":1,"name":"106.187.41.147:81","weight":1}]
[{"current_weight":0,"effective_weight":1,"fail_timeout":10,"fails":0,"id":0,"max_fails":1,"name":"127.0.0.2:80","weight":1}]
--- no_error_log
[error]



=== TEST 8: get backup peers
--- http_config
    $TEST_NGINX_MY_INIT_CONFIG
    upstream foo.com:1234 {
        server 127.0.0.6 fail_timeout=5 backup;
        server 127.0.0.1 fail_timeout=53 weight=4 max_fails=100;
        server agentzh.org:81;
    }

    upstream bar {
        server 127.0.0.2;
        server 127.0.0.3 backup;
        server 127.0.0.4 fail_timeout=23 weight=7 max_fails=200 backup;
    }
--- config
    location /t {
        content_by_lua '
            local upstream = require "ngx.upstream"
            local ljson = require "ljson"
            us = upstream.get_upstreams()
            for _, u in ipairs(us) do
                local peers, err = upstream.get_backup_peers(u)
                if not peers then
                    ngx.say("failed to get peers: ", err)
                    return
                end
                ngx.say(ljson.encode(peers))
            end
        ';
    }
--- request
    GET /t
--- response_body
[{"current_weight":0,"effective_weight":1,"fail_timeout":5,"fails":0,"id":0,"max_fails":1,"name":"127.0.0.6:80","weight":1}]
[{"current_weight":0,"effective_weight":1,"fail_timeout":10,"fails":0,"id":0,"max_fails":1,"name":"127.0.0.3:80","weight":1},{"current_weight":0,"effective_weight":7,"fail_timeout":23,"fails":0,"id":1,"max_fails":200,"name":"127.0.0.4:80","weight":7}]
--- no_error_log
[error]



=== TEST 9: set primary peer down (0)
--- http_config
    $TEST_NGINX_MY_INIT_CONFIG
    upstream bar {
        server 127.0.0.2;
        server 127.0.0.3 backup;
        server 127.0.0.4 fail_timeout=23 weight=7 max_fails=200 backup;
    }
--- config
    location /t {
        content_by_lua '
            local upstream = require "ngx.upstream"
            local ljson = require "ljson"
            local u = "bar"
            local ok, err = upstream.set_peer_down(u, false, 0, true)
            if not ok then
                ngx.say("failed to set peer down: ", err)
                return
            end
            local peers, err = upstream.get_primary_peers(u)
            if not peers then
                ngx.say("failed to get peers: ", err)
                return
            end
            ngx.say(ljson.encode(peers))
        ';
    }
--- request
    GET /t
--- response_body
[{"current_weight":0,"down":true,"effective_weight":1,"fail_timeout":10,"fails":0,"id":0,"max_fails":1,"name":"127.0.0.2:80","weight":1}]
--- no_error_log
[error]



=== TEST 10: set primary peer down (1, bad index)
--- http_config
    $TEST_NGINX_MY_INIT_CONFIG
    upstream bar {
        server 127.0.0.2;
        server 127.0.0.3 backup;
        server 127.0.0.4 fail_timeout=23 weight=7 max_fails=200 backup;
    }
--- config
    location /t {
        content_by_lua '
            local upstream = require "ngx.upstream"
            local ljson = require "ljson"
            local u = "bar"
            local ok, err = upstream.set_peer_down(u, false, 1, true)
            if not ok then
                ngx.say("failed to set peer down: ", err)
                return
            end
            local peers, err = upstream.get_primary_peers(u)
            if not peers then
                ngx.say("failed to get peers: ", err)
                return
            end
            ngx.say(ljson.encode(peers))
        ';
    }
--- request
    GET /t
--- response_body
failed to set peer down: bad peer id
--- no_error_log
[error]



=== TEST 11: set backup peer down (index 0)
--- http_config
    $TEST_NGINX_MY_INIT_CONFIG
    upstream bar {
        server 127.0.0.2;
        server 127.0.0.3 backup;
        server 127.0.0.4 fail_timeout=23 weight=7 max_fails=200 backup;
    }
--- config
    location /t {
        content_by_lua '
            local upstream = require "ngx.upstream"
            local ljson = require "ljson"
            local u = "bar"
            local ok, err = upstream.set_peer_down(u, true, 0, true)
            if not ok then
                ngx.say("failed to set peer down: ", err)
                return
            end
            local peers, err = upstream.get_backup_peers(u)
            if not peers then
                ngx.say("failed to get peers: ", err)
                return
            end
            ngx.say(ljson.encode(peers))
        ';
    }
--- request
    GET /t
--- response_body
[{"current_weight":0,"down":true,"effective_weight":1,"fail_timeout":10,"fails":0,"id":0,"max_fails":1,"name":"127.0.0.3:80","weight":1},{"current_weight":0,"effective_weight":7,"fail_timeout":23,"fails":0,"id":1,"max_fails":200,"name":"127.0.0.4:80","weight":7}]
--- no_error_log
[error]



=== TEST 12: set backup peer down (toggle twice, index 0)
--- http_config
    $TEST_NGINX_MY_INIT_CONFIG
    upstream bar {
        server 127.0.0.2;
        server 127.0.0.3 backup;
        server 127.0.0.4 fail_timeout=23 weight=7 max_fails=200 backup;
    }
--- config
    location /t {
        content_by_lua '
            local upstream = require "ngx.upstream"
            local ljson = require "ljson"
            local u = "bar"
            local ok, err = upstream.set_peer_down(u, true, 0, true)
            if not ok then
                ngx.say("failed to set peer down: ", err)
                return
            end
            local ok, err = upstream.set_peer_down(u, true, 0, false)
            if not ok then
                ngx.say("failed to set peer down: ", err)
                return
            end

            local peers, err = upstream.get_backup_peers(u)
            if not peers then
                ngx.say("failed to get peers: ", err)
                return
            end
            ngx.say(ljson.encode(peers))
        ';
    }
--- request
    GET /t
--- response_body
[{"current_weight":0,"effective_weight":1,"fail_timeout":10,"fails":0,"id":0,"max_fails":1,"name":"127.0.0.3:80","weight":1},{"current_weight":0,"effective_weight":7,"fail_timeout":23,"fails":0,"id":1,"max_fails":200,"name":"127.0.0.4:80","weight":7}]
--- no_error_log
[error]



=== TEST 13: set backup peer down (index 1)
--- http_config
    $TEST_NGINX_MY_INIT_CONFIG
    upstream bar {
        server 127.0.0.2;
        server 127.0.0.3 backup;
        server 127.0.0.4 fail_timeout=23 weight=7 max_fails=200 backup;
    }
--- config
    location /t {
        content_by_lua '
            local upstream = require "ngx.upstream"
            local ljson = require "ljson"
            local u = "bar"
            local ok, err = upstream.set_peer_down(u, true, 1, true)
            if not ok then
                ngx.say("failed to set peer down: ", err)
                return
            end

            local peers, err = upstream.get_backup_peers(u)
            if not peers then
                ngx.say("failed to get peers: ", err)
                return
            end
            ngx.say(ljson.encode(peers))
        ';
    }
--- request
    GET /t
--- response_body
[{"current_weight":0,"effective_weight":1,"fail_timeout":10,"fails":0,"id":0,"max_fails":1,"name":"127.0.0.3:80","weight":1},{"current_weight":0,"down":true,"effective_weight":7,"fail_timeout":23,"fails":0,"id":1,"max_fails":200,"name":"127.0.0.4:80","weight":7}]
--- no_error_log
[error]



=== TEST 14: upstream names with ports (github #2)
--- http_config
--- config
    location /upstream1 {
        proxy_pass http://127.0.0.1:1190;
    }

    location /upstream2{
        proxy_pass http://127.0.0.2:1110;
    }

    location /upstream3{
        proxy_pass http://127.0.0.1:1130;
    }

    location /t {
        content_by_lua '
                local concat = table.concat
                local upstream = require "ngx.upstream"
                local get_servers = upstream.get_servers
                local get_upstreams = upstream.get_upstreams

                local us = get_upstreams()
                for _, u in ipairs(us) do
                    ngx.say("upstream ", u, ":")
                    local srvs, err = get_servers(u)
                    if not srvs then
                        ngx.say("failed to get servers in upstream ", u)
                    else
                        for _, srv in ipairs(srvs) do
                            local first = true
                            for k, v in pairs(srv) do
                                if first then
                                    first = false
                                    ngx.print("    ")
                                else
                                    ngx.print(", ")
                                end
                                if type(v) == "table" then
                                    ngx.print(k, " = {", concat(v, ", "), "}")
                                else
                                    ngx.print(k, " = ", v)
                                end
                            end
                            ngx.print("\\n")
                        end
                    end
                end
        ';
    }
--- request
    GET /t
--- response_body
upstream 127.0.0.1:1190:
    addr = 127.0.0.1:1190, weight = 0, fail_timeout = 0, max_fails = 0
upstream 127.0.0.2:1110:
    addr = 127.0.0.2:1110, weight = 0, fail_timeout = 0, max_fails = 0
upstream 127.0.0.1:1130:
    addr = 127.0.0.1:1130, weight = 0, fail_timeout = 0, max_fails = 0

--- no_error_log
[error]

=== TEST 15: add server with upstream
--- http_config
     upstream foo.com {
        server agentzh.org:81;
    }

    upstream bar {
        server 127.0.0.1:81;
    }
--- config
        location  /add_server {

                default_type text/plain;
                content_by_lua '

                        local concat = table.concat
                        local upstream = require "ngx.upstream"
                        local get_servers = upstream.get_servers
                        local get_upstreams = upstream.get_upstreams
                        local add_server = upstream.add_server

                        local args =  ngx.req.get_uri_args()
                        local upstream_name

                        upstream_name = args["upstream"]
                        local server_ip = args["ip"]
                        local server_port = args["port"]
                        local weight = 2
                        local max_fails = 10
                        local fail_timeout = 10


                        -- ngx.say("upstream ", ":", upstream_name )
                        -- ngx.say("server ", ":", server_ip..":"..server_port)

                        local str,err = add_server("bar",server_ip..":"..server_port,weight,max_fails,fail_timeout)
                        if not str then
                            ngx.say("the server is exist :",server_ip..":"..server_port)
                            -- ngx.say(str)
                        end

                        ngx.print("----------------------------\\n")

                        local srvs, err = get_servers(upstream_name)
                        if not srvs then
                                ngx.say("failed to get servers: ",err)
                        else
                                for _, srv in ipairs(srvs) do
                                        local first = true
                                        for k, v in pairs(srv) do
                                                if first then
                                                        first = false
                                                        ngx.print("    ")
                                                else
                                                        ngx.print(", ")
                                                end
                                                if type(v) == "table" then
                                                        ngx.print(k, " = {", concat(v, ", "), "}")
                                                else
                                                        ngx.print(k, " = ", v)
                                                end
                                        end
                                        ngx.print("\\n")
                                end
                        end


                ';
        }

--- request
    GET /add_server?upstream=bar&ip=127.0.0.1&port=80
--- response_body

----------------------------
    addr = 127.0.0.1:81, weight = 1, fail_timeout = 10, max_fails = 1
    addr = 127.0.0.1:80, weight = 2, fail_timeout = 10, max_fails = 10
--- no_error_log
[error]


=== TEST 16: add peer with upstream'peers
--- http_config
     upstream foo.com {
        server 127.0.0.1 fail_timeout=53 weight=4 max_fails=100;
        server agentzh.org:81;
    }

    upstream bar {
        server 127.0.0.1:81   weight=1;
    }
--- config

        location  /add_peer {

                default_type text/plain;
                content_by_lua '

                        local concat = table.concat
                        local upstream = require "ngx.upstream"
                        -- local get_servers = upstream.get_servers
                        -- local get_upstreams = upstream.get_upstreams
                        local add_peer = upstream.add_peer

                        local args =  ngx.req.get_uri_args()
                        local upstream_name

                        upstream_name = args["upstream"]
                        local server_ip = args["ip"]
                        local server_port = args["port"]

                        ngx.say("server ", ":", server_ip..":"..server_port)

                        local str,err = add_peer(upstream_name,server_ip..":"..server_port)
                        --      ngx.say(str,err)
                        if not str then
                                ngx.say(err)
                        end
                        -- ngx.print(err)
                        ngx.print("\\n----------------------------\\n")
                        --[[
                        local srvs, err = get_servers(upstream_name)
                        if not srvs then
                                ngx.say("failed to get servers in upstream ", upstream_name)
                        else
                                for _, srv in ipairs(srvs) do
                                        local first = true
                                        for k, v in pairs(srv) do
                                                if first then
                                                        first = false
                                                        ngx.print("    ")
                                                else
                                                        ngx.print(", ")
                                                end
                                                if type(v) == "table" then
                                                        ngx.print(k, " = {", concat(v, ", "), "}")
                                                else
                                                        ngx.print(k, " = ", v)
                                                end
                                        end
                                        ngx.print("\\n")
                                end
                        end
                    ]]

                ';
        }

--- request
    GET /add_peer?upstream=bar&ip=127.0.0.1&port=80
--- response_body
server :127.0.0.1:80
not find this peer


----------------------------
--- no_error_log
[error]

=== TEST 17: remove server from  upstream
--- http_config
     upstream foo.com {
        server 127.0.0.1 fail_timeout=53 weight=4 max_fails=100;
        server agentzh.org:81;
    }

    upstream bar {
        server 127.0.0.1:81   weight=1;
    }
--- config

      location /remove_server {
          content_by_lua '
              local upstream = require "ngx.upstream"
              local remove_server = upstream.remove_server
              local args =  ngx.req.get_uri_args()
              upstream_name = args["upstream"]
              local server_ip = args["ip"]
              local server_port = args["port"]

              local ser, err = remove_server(upstream_name,server_ip..":"..server_port)
              if not ser then
                 ngx.say("failed to remove server: ", err)
                 return
              end
           ';
       }

--- request
    GET /remove_server?upstream=bar&ip=127.0.0.1&port=81
--- response_body
--- no_error_log
[error]

=== TEST 18: remove peer from  upstream
--- http_config
     upstream foo.com {
        server 127.0.0.1 fail_timeout=53 weight=4 max_fails=100;
        server agentzh.org:81;
    }

    upstream bar {
        server 127.0.0.1:81   weight=1;
    }
--- config

      location /remove_peer {
          content_by_lua '
              local upstream = require "ngx.upstream"
              local remove_peer = upstream.remove_peer
              local args =  ngx.req.get_uri_args()
              upstream_name = args["upstream"]
              local server_ip = args["ip"]
              local server_port = args["port"]

              local ser, err = remove_peer(upstream_name,server_ip..":"..server_port)
              if not ser then
                 ngx.say("failed to remove peer: ", err)
                 return
              end
           ';
       }

--- request
--- request
    GET /remove_peer?upstream=bar&ip=127.0.0.1&port=81
--- response_body
--- no_error_log
[error]

