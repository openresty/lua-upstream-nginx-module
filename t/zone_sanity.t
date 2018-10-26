# vim:set ft= ts=4 sw=4 et fdm=marker:

use Test::Nginx::Socket::Lua;

#worker_connections(1014);
master_on();
workers(4);
#log_level('warn');

repeat_each(2);

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
        zone shared 64k;
        server 127.0.0.1;
    }

    upstream bar {
        zone shared;
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
        zone shared 64k;
        server 127.0.0.1 fail_timeout=53 weight=4 max_fails=100;
        server agentzh.org:81 backup;
    }

    upstream bar {
        zone shared;
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
foo.com:1234: [{"addr":"127.0.0.1:80","fail_timeout":53,"max_fails":100,"name":"127.0.0.1","weight":4},{"addr":"172.105.207.225:81","backup":true,"fail_timeout":10,"max_fails":1,"name":"agentzh.org:81","weight":1}]
bar: [{"addr":"127.0.0.2:80","fail_timeout":10,"max_fails":1,"name":"127.0.0.2","weight":1}]
failed to get servers: upstream not found

--- no_error_log
[error]



=== TEST 4: sample in README
--- http_config
    upstream foo.com {
        zone shared 64k;
        server 127.0.0.1 fail_timeout=53 weight=4 max_fails=100;
        server agentzh.org:81;
    }

    upstream bar {
        zone shared;
        server 127.0.0.2;
    }

--- config
    location = /upstreams {
        default_type text/plain;
        content_by_lua_block {
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
                        local i = 0
                        local keys = {}
                        for k, _ in pairs(srv) do
                            i = i + 1
                            keys[i] = k
                        end
                        table.sort(keys)
                        for _, k in ipairs(keys) do
                            local v = srv[k]
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
                        ngx.print("\n")
                    end
                end
            end
        }
    }
--- request
    GET /upstreams
--- response_body
upstream foo.com:
    addr = 127.0.0.1:80, fail_timeout = 53, max_fails = 100, name = 127.0.0.1, weight = 4
    addr = 172.105.207.225:81, fail_timeout = 10, max_fails = 1, name = agentzh.org:81, weight = 1
upstream bar:
    addr = 127.0.0.2:80, fail_timeout = 10, max_fails = 1, name = 127.0.0.2, weight = 1
--- no_error_log
[error]



=== TEST 5: multi-peer servers
--- http_config
    $TEST_NGINX_MY_INIT_CONFIG
    upstream test {
        zone shared 64k;
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
^\[\{"addr":\["\d{1,3}(?:\.\d{1,3}){3}:80"(?:,"\d{1,3}(?:\.\d{1,3}){3}:80")+\],"fail_timeout":10,"max_fails":1,"name":"multi-ip-test\.openresty\.com","weight":1\}\]$

--- no_error_log
[error]



=== TEST 6: get primary peers
--- http_config
    $TEST_NGINX_MY_INIT_CONFIG
    upstream foo.com:1234 {
        zone shared 64k;
        server 127.0.0.6 fail_timeout=5 backup;
        server 127.0.0.1 fail_timeout=53 weight=4 max_fails=100;
        server agentzh.org:81;
    }

    upstream bar {
        zone shared;
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
[{"conns":0,"current_weight":0,"effective_weight":4,"fail_timeout":53,"fails":0,"id":0,"max_fails":100,"name":"127.0.0.1:80","weight":4},{"conns":0,"current_weight":0,"effective_weight":1,"fail_timeout":10,"fails":0,"id":1,"max_fails":1,"name":"172.105.207.225:81","weight":1}]
[{"conns":0,"current_weight":0,"effective_weight":1,"fail_timeout":10,"fails":0,"id":0,"max_fails":1,"name":"127.0.0.2:80","weight":1}]
--- no_error_log
[error]



=== TEST 7: get backup peers
--- http_config
    $TEST_NGINX_MY_INIT_CONFIG
    upstream foo.com:1234 {
        zone shared 64k;
        server 127.0.0.6 fail_timeout=5 backup;
        server 127.0.0.1 fail_timeout=53 weight=4 max_fails=100;
        server agentzh.org:81;
    }

    upstream bar {
        zone shared;
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
[{"conns":0,"current_weight":0,"effective_weight":1,"fail_timeout":5,"fails":0,"id":0,"max_fails":1,"name":"127.0.0.6:80","weight":1}]
[{"conns":0,"current_weight":0,"effective_weight":1,"fail_timeout":10,"fails":0,"id":0,"max_fails":1,"name":"127.0.0.3:80","weight":1},{"conns":0,"current_weight":0,"effective_weight":7,"fail_timeout":23,"fails":0,"id":1,"max_fails":200,"name":"127.0.0.4:80","weight":7}]
--- no_error_log
[error]



=== TEST 8: set primary peer down (0)
--- http_config
    $TEST_NGINX_MY_INIT_CONFIG
    upstream bar {
        zone shared 64k;
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
[{"conns":0,"current_weight":0,"down":true,"effective_weight":1,"fail_timeout":10,"fails":0,"id":0,"max_fails":1,"name":"127.0.0.2:80","weight":1}]
--- no_error_log
[error]



=== TEST 9: set primary peer down (1, bad index)
--- http_config
    $TEST_NGINX_MY_INIT_CONFIG
    upstream bar {
        zone shared 64k;
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



=== TEST 10: set backup peer down (index 0)
--- http_config
    $TEST_NGINX_MY_INIT_CONFIG
    upstream bar {
        zone shared 64k;
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
[{"conns":0,"current_weight":0,"down":true,"effective_weight":1,"fail_timeout":10,"fails":0,"id":0,"max_fails":1,"name":"127.0.0.3:80","weight":1},{"conns":0,"current_weight":0,"effective_weight":7,"fail_timeout":23,"fails":0,"id":1,"max_fails":200,"name":"127.0.0.4:80","weight":7}]
--- no_error_log
[error]



=== TEST 11: set backup peer down (toggle twice, index 0)
--- http_config
    $TEST_NGINX_MY_INIT_CONFIG
    upstream bar {
        zone shared 64k;
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
[{"conns":0,"current_weight":0,"effective_weight":1,"fail_timeout":10,"fails":0,"id":0,"max_fails":1,"name":"127.0.0.3:80","weight":1},{"conns":0,"current_weight":0,"effective_weight":7,"fail_timeout":23,"fails":0,"id":1,"max_fails":200,"name":"127.0.0.4:80","weight":7}]
--- no_error_log
[error]



=== TEST 12: set backup peer down (index 1)
--- http_config
    $TEST_NGINX_MY_INIT_CONFIG
    upstream bar {
        zone shared 64k;
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
[{"conns":0,"current_weight":0,"effective_weight":1,"fail_timeout":10,"fails":0,"id":0,"max_fails":1,"name":"127.0.0.3:80","weight":1},{"conns":0,"current_weight":0,"down":true,"effective_weight":7,"fail_timeout":23,"fails":0,"id":1,"max_fails":200,"name":"127.0.0.4:80","weight":7}]
--- no_error_log
[error]



=== TEST 13: upstream_name with valid explicit upstream
--- http_config
    upstream some_upstream {
        zone shared 64k;
        server 127.0.0.1:$TEST_NGINX_SERVER_PORT;
    }
--- config
    log_by_lua_block {
        local upstream = require "ngx.upstream"
        ngx.log(ngx.INFO, "upstream = " .. tostring(upstream.current_upstream_name()))
    }
    location /test {
        proxy_pass http://some_upstream/back;
    }
    location /back {
        echo ok;
    }
--- request
GET /test
--- response_body
ok
--- log_level: info
--- error_log eval
qr/upstream = some_upstream/
