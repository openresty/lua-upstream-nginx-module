# vim:set ft= ts=4 sw=4 et fdm=marker:

use Test::Nginx::Socket::Lua;

#worker_connections(1014);
#master_on();
#workers(2);
#log_level('warn');

plan tests => repeat_each() * (blocks() * 3);

$ENV{TEST_NGINX_MEMCACHED_PORT} ||= 11211;

$ENV{TEST_NGINX_MY_INIT_CONFIG} = <<_EOC_;
lua_package_path "t/lib/?.lua;;";
_EOC_

#no_diff();
no_long_string();
run_tests();

__DATA__

=== TEST 1: get_next_peer() - round robin load balancer
--- http_config
    upstream test_upstream {
        server 127.0.0.1:1190;
        server 127.0.0.2:1110;
        server 127.0.0.3:1130;
    }
--- config
    location /t {
        content_by_lua_block {
            local upstream = require "ngx.upstream"
            local peer = upstream.get_next_peer("test_upstream")
            ngx.print(peer)
        }
    }
--- request
    GET /t
--- response_body_like: 127.0.0.[123]:11[139]0
--- no_error_log
[error]


=== TEST 2: get_next_peer() - hash load balancer
--- http_config
    upstream test_upstream {
        hash abcd123;
        server 127.0.0.1:1190;
        server 127.0.0.2:1110;
        server 127.0.0.3:1130;
    }
--- config
    location /t {
        content_by_lua_block {
            local upstream = require "ngx.upstream"
            local peer = upstream.get_next_peer("test_upstream")
            ngx.print(peer)
        }
    }
--- request
    GET /t
--- response_body: 127.0.0.3:1130
--- no_error_log
[error]


=== TEST 3: get_next_peer() - hash (ketama) load balancer
--- http_config
    upstream test_upstream {
        hash abcd123 consistent;
        server 127.0.0.1:1190;
        server 127.0.0.2:1110;
        server 127.0.0.3:1130;
    }
--- config
    location /t {
        content_by_lua_block {
            local upstream = require "ngx.upstream"
            local peer = upstream.get_next_peer("test_upstream")
            ngx.print(peer)
        }
    }
--- request
    GET /t
--- response_body: 127.0.0.3:1130
--- no_error_log
[error]


=== TEST 4: get_next_peer() - ip_hash load balancer
--- http_config
    upstream test_upstream {
        ip_hash;
        server 127.0.0.1:1190;
        server 127.0.0.2:1110;
        server 127.0.0.3:1130;
    }
--- config
    location /t {
        content_by_lua_block {
            local upstream = require "ngx.upstream"
            local peer = upstream.get_next_peer("test_upstream")
            ngx.print(peer)
        }
    }
--- request
    GET /t
--- response_body: 127.0.0.3:1130
--- no_error_log
[error]


=== TEST 5: call in init phase should fail gracefully
--- http_config
    upstream test_upstream {
        ip_hash;
        server 127.0.0.1:1190;
        server 127.0.0.2:1110;
        server 127.0.0.3:1130;
    }
    init_by_lua_block {
        local upstream = require "ngx.upstream"
        local status, err = upstream.get_next_peer("test_upstream")
        ngx.log(ngx.ERR, err)
    }
--- config
    location /t {
        echo -n "OK";
    }
--- request
    GET /t
--- response_body: OK
--- error_log
request not available

