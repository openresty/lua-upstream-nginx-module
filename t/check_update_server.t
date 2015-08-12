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

=== TEST 1: add server with upstream
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

            local str,err = add_server("bar",server_ip..":"..server_port,weight,max_fails,fail_timeout)
            if not str then
               ngx.say("the server is exist :",server_ip..":"..server_port)
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



=== TEST 2: add peer with upstream'peers
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
            local add_peer = upstream.add_peer

            local args =  ngx.req.get_uri_args()
            local upstream_name

            upstream_name = args["upstream"]
            local server_ip = args["ip"]
            local server_port = args["port"]

            ngx.say("server ", ":", server_ip..":"..server_port)

            local str,err = add_peer(upstream_name,server_ip..":"..server_port)
            if not str then
                   ngx.say(err)
            end
            ngx.print("\\n----------------------------\\n")
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



=== TEST 3: remove server from  upstream
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



=== TEST 4: remove peer from  upstream
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

