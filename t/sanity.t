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
        server agentzh.org:81;
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
foo.com:1234: [{"addr":"127.0.0.1:80","fail_timeout":53,"max_fails":100,"weight":4},{"addr":"106.187.41.147:81","fail_timeout":10,"max_fails":1,"weight":1}]
bar: [{"addr":"127.0.0.2:80","fail_timeout":10,"max_fails":1,"weight":1}]
failed to get servers: upstream not found

--- no_error_log
[error]



=== TEST 1: sample
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
                            ngx.print(k, " = ", v)
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

