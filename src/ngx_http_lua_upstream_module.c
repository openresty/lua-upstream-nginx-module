
/*
 * Copyright (C) Yichun Zhang (agentzh)
 */


#ifndef DDEBUG
#define DDEBUG 0
#endif
#include "ddebug.h"


#include <ngx_core.h>
#include <ngx_http.h>
#include <lauxlib.h>
#include "ngx_http_lua_api.h"
#include "ngx_http_lua_upstream_module.h"


#define NGX_DELAY_DELETE 100 * 1000

ngx_module_t ngx_http_lua_upstream_module;


static ngx_int_t ngx_http_lua_upstream_init(ngx_conf_t *cf);
static int ngx_http_lua_upstream_create_module(lua_State * L); 
static int ngx_http_lua_upstream_get_upstreams(lua_State * L); 
static int ngx_http_lua_upstream_get_servers(lua_State * L); 
static ngx_http_upstream_main_conf_t *
    ngx_http_lua_upstream_get_upstream_main_conf(lua_State *L);
static int ngx_http_lua_upstream_get_primary_peers(lua_State * L); 
static int ngx_http_lua_upstream_get_backup_peers(lua_State * L); 
static int ngx_http_lua_get_peer(lua_State *L, 
    ngx_http_upstream_rr_peer_t *peer, ngx_uint_t id);
static ngx_http_upstream_srv_conf_t *
    ngx_http_lua_upstream_find_upstream(lua_State *L, ngx_str_t *host);
static ngx_http_upstream_rr_peer_t *
    ngx_http_lua_upstream_lookup_peer(lua_State *L);
static int ngx_http_lua_upstream_set_peer_down(lua_State * L);
static int ngx_http_lua_upstream_add_server(lua_State * L); 
static ngx_http_upstream_server_t*
    ngx_http_lua_upstream_compare_server(ngx_http_upstream_srv_conf_t * us, ngx_url_t u); 
static ngx_http_upstream_srv_conf_t *
    ngx_http_lua_upstream_check_peers(lua_State * L, ngx_url_t u, ngx_http_upstream_server_t ** srv);
static int 
    ngx_http_lua_upstream_exist_peer(ngx_http_upstream_rr_peers_t * peers, ngx_url_t u); 
static int ngx_http_lua_upstream_add_peer(lua_State * L); 
static int ngx_http_lua_upstream_remove_server(lua_State * L); 
static int ngx_http_lua_upstream_remove_peer(lua_State * L);
static void ngx_http_lua_upstream_event_init(void *peers);
static void ngx_http_lua_upstream_add_delay_delete(ngx_event_t *event);
static ngx_int_t ngx_pfree_and_delay(ngx_pool_t *pool, void *p);    
static void * ngx_prealloc(ngx_pool_t *pool, void *p, size_t old_size, size_t new_size, ngx_uint_t flag);  

#if (NGX_HTTP_UPSTREAM_CONSISTENT_HASH)

static int 
ngx_http_upstream_chash_cmp_points(const void *one, const void *two);
static void
ngx_http_upstream_consistent_hash(ngx_http_upstream_rr_peer_t *peer, ngx_http_upstream_chash_points_t *points);
static int 
ngx_http_lua_upstream_consistent_hash_init(lua_State * L, ngx_http_upstream_srv_conf_t *uscf);
static int
ngx_http_lua_upstream_remove_peer_chash(lua_State * L, ngx_http_upstream_srv_conf_t *us);

#endif

#if (NGX_HTTP_UPSTREAM_LEAST_CONN)

static int
ngx_http_lua_upstream_least_conn_init(lua_State * L, ngx_http_upstream_srv_conf_t *uscf, ngx_uint_t flag);
static int
ngx_http_lua_upstream_remover_peer_least_conn(lua_State * L, ngx_http_upstream_srv_conf_t *us, ngx_uint_t pos);

#endif

static ngx_http_module_t ngx_http_lua_upstream_ctx = {
    NULL,                           /* preconfiguration */
    ngx_http_lua_upstream_init,     /* postconfiguration */
    NULL,                           /* create main configuration */
    NULL,                           /* init main configuration */
    NULL,                           /* create server configuration */
    NULL,                           /* merge server configuration */
    NULL,                           /* create location configuration */
    NULL                            /* merge location configuration */
};


ngx_module_t ngx_http_lua_upstream_module = {
    NGX_MODULE_V1,
    &ngx_http_lua_upstream_ctx,  /* module context */
    NULL,                        /* module directives */
    NGX_HTTP_MODULE,             /* module type */
    NULL,                        /* init master */
    NULL,                        /* init module */
    NULL,                        /* init process */
    NULL,                        /* init thread */
    NULL,                        /* exit thread */
    NULL,                        /* exit process */
    NULL,                        /* exit master */
    NGX_MODULE_V1_PADDING
};


typedef struct {
    ngx_event_t                              delay_delete_ev;

    time_t                                   start_sec;
    ngx_msec_t                               start_msec;

    void                                    *data;
} ngx_delay_event_t;


static void
ngx_http_lua_upstream_event_init(void *peers)
{
    ngx_time_t                                  *tp;
    ngx_delay_event_t                           *delay_event;


    delay_event = ngx_calloc(sizeof(*delay_event), ngx_cycle->log);
    if (delay_event == NULL) {
        ngx_log_error(NGX_LOG_ERR, ngx_cycle->log, 0,
                "http_lua_upstream_event_init: calloc failed");
        return;
    }

    tp = ngx_timeofday();
    delay_event->start_sec = tp->sec;
    delay_event->start_msec = tp->msec;

    delay_event->delay_delete_ev.handler = ngx_http_lua_upstream_add_delay_delete;
    delay_event->delay_delete_ev.log = ngx_cycle->log;
    delay_event->delay_delete_ev.data = delay_event;
    delay_event->delay_delete_ev.timer_set = 0;


    delay_event->data = peers;
    ngx_add_timer(&delay_event->delay_delete_ev, NGX_DELAY_DELETE);

    return;
}


static void
ngx_http_lua_upstream_add_delay_delete(ngx_event_t *event)
{
    ngx_uint_t                     i;
    ngx_connection_t              *c;
    ngx_delay_event_t             *delay_event;
    ngx_http_request_t            *r=NULL;
    ngx_http_log_ctx_t            *ctx=NULL;
    void                          *peers=NULL;

    delay_event = event->data;

    c = ngx_cycle->connections;
    for (i = 0; i < ngx_cycle->connection_n; i++) {

        if (c[i].fd == (ngx_socket_t) - 1) {
            continue;
        } else {

            if (c[i].log->data != NULL) {
                ctx = c[i].log->data;
                r = ctx->current_request;
            }
        }

        if (r) {
            if (r->start_sec < delay_event->start_sec) {
                ngx_add_timer(&delay_event->delay_delete_ev, NGX_DELAY_DELETE);
                return;
            }

            if (r->start_sec == delay_event->start_sec) {

                if (r->start_msec <= delay_event->start_msec) {
                    ngx_add_timer(&delay_event->delay_delete_ev, NGX_DELAY_DELETE);
                    return;
                }
            }
        }
    }

    peers = delay_event->data;

    if (peers != NULL) {

        ngx_free(peers);
        peers = NULL;
    }

 
    ngx_free(delay_event);

    delay_event = NULL;

    return;
}


static ngx_int_t
ngx_http_lua_upstream_init(ngx_conf_t *cf)
{
    if (ngx_http_lua_add_package_preload(cf, "ngx.upstream",
                                         ngx_http_lua_upstream_create_module)
        != NGX_OK)
    {
        return NGX_ERROR;
    }

    return NGX_OK;
}


static int
ngx_http_lua_upstream_create_module(lua_State * L)
{
    lua_createtable(L, 0, 1);

    lua_pushcfunction(L, ngx_http_lua_upstream_get_upstreams);
    lua_setfield(L, -2, "get_upstreams");

    lua_pushcfunction(L, ngx_http_lua_upstream_get_servers);
    lua_setfield(L, -2, "get_servers");

    lua_pushcfunction(L, ngx_http_lua_upstream_get_primary_peers);
    lua_setfield(L, -2, "get_primary_peers");

    lua_pushcfunction(L, ngx_http_lua_upstream_get_backup_peers);
    lua_setfield(L, -2, "get_backup_peers");

    lua_pushcfunction(L, ngx_http_lua_upstream_set_peer_down);
    lua_setfield(L, -2, "set_peer_down");

    lua_pushcfunction(L, ngx_http_lua_upstream_add_server);
    lua_setfield(L, -2, "add_server");

    lua_pushcfunction(L, ngx_http_lua_upstream_add_peer);
    lua_setfield(L, -2, "add_peer");

    lua_pushcfunction(L, ngx_http_lua_upstream_remove_server);
    lua_setfield(L, -2, "remove_server");

    lua_pushcfunction(L, ngx_http_lua_upstream_remove_peer);
    lua_setfield(L, -2, "remove_peer");

    return 1;
}


/*
 * The function is compare server as upstream server 
 * if exists and return upstream_server_t else return 
 * NULL.
 * 
*/
static ngx_http_upstream_server_t*
ngx_http_lua_upstream_compare_server(ngx_http_upstream_srv_conf_t * us, ngx_url_t u )
{
    ngx_uint_t                       i, j;
    size_t                           len;
    ngx_http_upstream_server_t      *server = NULL;

    if (us->servers == NULL || us->servers->nelts == 0) {
        return NULL;
    }

    server = us->servers->elts;

    for (i = 0; i < us->servers->nelts; ++i) {
        for(j = 0; j < server[i].naddrs; ++j) {

            len = server[i].addrs[j].name.len;
            if (len == u.url.len 
                 && ngx_memcmp(u.url.data, server[i].addrs[j].name.data, u.url.len) == 0) {

                return  &server[i];
            } 
         }
    }

    return NULL;
}


/*
 * The function is dynamically add a server to upstream 
 * server ,but not added it to the back-end peer.
 * 
*/
static int
ngx_http_lua_upstream_add_server(lua_State * L)
{

    ngx_str_t                         host;
    ngx_http_upstream_server_t       *us;
    ngx_http_upstream_srv_conf_t     *uscf;
    ngx_url_t                         u;
    ngx_http_request_t               *r;
    ngx_int_t                         weight, max_fails;
    time_t                            fail_timeout;
    u_char                           *p;

    if (lua_gettop(L) != 5) {
        // five param is :"upstream name", "ip:port" , "weight" , "max_fails", 
        //"fail_time"
        // for lua code , you must pass this five param, is none ,you should 
        // consider pass default value.
        return luaL_error(L, "exactly five argument expected");
    }

    r = ngx_http_lua_get_request(L);
    if (r == NULL) {
        lua_pushnil(L);
        lua_pushliteral(L, "get request error \n");
        return 2;
    }

    host.data = (u_char *) luaL_checklstring(L, 1, &host.len);

    ngx_memzero(&u, sizeof (ngx_url_t));
    p = (u_char *) luaL_checklstring(L, 2, &u.url.len);
    u.default_port = 80;

    weight = (ngx_int_t) luaL_checkint(L, 3);
    max_fails = (ngx_int_t) luaL_checkint(L, 4);
    fail_timeout = (time_t) luaL_checklong(L, 5);

#if (NGX_DEBUG)
    ngx_log_error(NGX_LOG_ALERT, r->connection->log, 0, "%s %s params: %s,%s,%d,%d,%d\n", __FILE__,__FUNCTION__, host.data, p, weight, max_fails, fail_timeout);
#endif

    uscf = ngx_http_lua_upstream_find_upstream(L, &host);
    if (uscf == NULL) {
        lua_pushnil(L);
        lua_pushliteral(L, "upstream not found\n");
        return 2;
    }
     
    // lua virtual machine memory is stack,so dup a memory 
    u.url.data = ngx_pcalloc(uscf->servers->pool, u.url.len+1);
    ngx_memcpy(u.url.data, p, u.url.len);

    if (ngx_http_lua_upstream_compare_server(uscf, u) != NULL) {
        lua_pushnil(L);
        lua_pushliteral(L,"this server is exist\n");
        return 2;
    }    

    if (uscf->servers == NULL || uscf->servers->nelts == 0) {
        lua_pushliteral(L, "upstream has no server before!\n");
        lua_newtable(L);
        return 2;

    } else {
        if (ngx_parse_url(uscf->servers->pool, &u) != NGX_OK) {
            if (u.err) {
                lua_pushnil(L);
                lua_pushliteral(L, "url parser error");
                return 2;
            }
        }

        us = ngx_array_push(uscf->servers);
        if (us == NULL) {
            lua_pushnil(L);
            lua_pushliteral(L, "us push uscf->servers failed\n");
            return 2;
        }
       
        ngx_memzero(us, sizeof (ngx_http_upstream_server_t));

        us->name = u.url;
        us->addrs = u.addrs;
        us->naddrs = u.naddrs;
        us->weight = weight;
        us->max_fails = max_fails;
        us->fail_timeout = fail_timeout;
    }

    lua_pushboolean(L, 1);
    return 1;
}


/*
 * The function is remove a server from ngx_http_upstream_srv_conf_t servers.  
 * 
*/
static int
ngx_http_lua_upstream_remove_server(lua_State * L)
{
    ngx_uint_t                           i, j, k;
    size_t                               len;
    ngx_str_t                            host;
    ngx_array_t                         *servers;
    ngx_http_upstream_server_t          *us, *server;
    ngx_http_upstream_srv_conf_t        *uscf;
    ngx_http_request_t                  *r;
    ngx_url_t                            u;
    size_t                               old_size, new_size;
    
    if (lua_gettop(L) != 2) {
        // two param is :  "bar","ip:port" 
        // for lua code , you must pass this two param, is none ,you should 
        // consider pass default value.
        lua_pushnil(L);
        lua_pushliteral(L, "exactly two argument expected\n");
        return 2;
    }

    r = ngx_http_lua_get_request(L);
    if (r == NULL) {
        lua_pushnil(L);
        lua_pushliteral(L, "get request error \n");
        return 2;
    }

    host.data = (u_char *) luaL_checklstring(L, 1, &host.len);

    ngx_memzero(&u, sizeof (ngx_url_t));
    u.url.data = (u_char *) luaL_checklstring(L, 2, &u.url.len);
    u.default_port = 80;

#if (NGX_DEBUG)
    ngx_log_error(NGX_LOG_ALERT, r->connection->log, 0, "%s %s params: %s\n", __FILE__,__FUNCTION__ , u.url.data );
#endif

    uscf = ngx_http_lua_upstream_find_upstream(L, &host);
    if (uscf == NULL) {
        lua_pushnil(L);
        lua_pushliteral(L, "upstream not found\n");
        return 2;
    }
    
    if (ngx_http_lua_upstream_compare_server(uscf, u) == NULL) {
        lua_pushnil(L);
        lua_pushliteral(L,"not found this server\n");
        return 2;
    }
    
    if (uscf->servers->nelts == 1) { 
        lua_pushnil(L);
        lua_pushliteral(L, "upstream last one is not allowed to delete\n");
        return 2;
    }

    server = uscf->servers->elts;

    servers = ngx_array_create(ngx_cycle->pool, uscf->servers->nelts, sizeof(ngx_http_upstream_server_t));    
    if (servers == NULL) {
        lua_pushnil(L);
        lua_pushliteral(L, "servers create fail\n");
        return 2;
    }
    
    for (i = 0; i < uscf->servers->nelts; i++) {
        if (server[i].naddrs == 1) {

             len = server[i].addrs->name.len;
	     if (len == u.url.len 
                  && ngx_memcmp(u.url.data, server[i].addrs->name.data, u.url.len) == 0) {
                  continue;
             }

        } else {
             for (j = 0; j < server[i].naddrs; ++j) {

                 len = server[i].addrs[j].name.len;
	         if (len == u.url.len 
                      && ngx_memcmp(u.url.data, server[i].addrs[j].name.data, u.url.len) == 0) {
                      for (k = j; k < server[i].naddrs -1; ++k) {
                           server[i].addrs[k] = server[i].addrs[k+1];
                      }

                      old_size = server[i].naddrs * sizeof(ngx_addr_t);
                      new_size = (server[i].naddrs - 1) * sizeof(ngx_addr_t);

                      server[i].addrs = ngx_prealloc(ngx_cycle->pool, server[i].addrs, old_size, new_size, 0);
                      server[i].naddrs -= 1;
                      break;
                 }
            }
        }
              
        us = ngx_array_push(servers);
        ngx_memzero(us,sizeof(ngx_http_upstream_server_t));
        us->name = server[i].name;
        us->addrs = server[i].addrs;
        us->naddrs = server[i].naddrs;
        us->weight = server[i].weight;
        us->max_fails = server[i].max_fails;
        us->fail_timeout = server[i].fail_timeout;
    }
   
    ngx_array_destroy(uscf->servers);
    uscf->servers = servers;

    lua_pushboolean(L, 1);
    return 1;
}


#if (NGX_HTTP_UPSTREAM_CONSISTENT_HASH)

static int 
ngx_http_upstream_chash_cmp_points(const void *one, const void *two)
{
    ngx_http_upstream_chash_point_t *first =
                                       (ngx_http_upstream_chash_point_t *) one;
    ngx_http_upstream_chash_point_t *second =
                                       (ngx_http_upstream_chash_point_t *) two;

    if (first->hash < second->hash) {
        return -1;

    } else if (first->hash > second->hash) {
        return 1;

    } else {
        return 0;
    }
}

static void
ngx_http_upstream_consistent_hash(ngx_http_upstream_rr_peer_t *peer, 
                                          ngx_http_upstream_chash_points_t *points)
{
    ngx_str_t                             *server;
    u_char                                *host, *port, c;
    ngx_uint_t                             npoints, j;
    uint32_t                               hash, base_hash;
    size_t                                 host_len, port_len;
    union {
        uint32_t                            value;
        u_char                              byte[4];
    } prev_hash;


    server = &peer->server;
    if (server->len >= 5
        && ngx_strncasecmp(server->data, (u_char *) "unix:", 5) == 0)
    {
        host = server->data + 5;
        host_len = server->len - 5;
        port = NULL;
        port_len = 0;
        goto done;
    }

    for (j = 0; j < server->len; j++) {
        c = server->data[server->len - j - 1];

        if (c == ':') {
            host = server->data;
            host_len = server->len - j - 1;
            port = server->data + server->len - j;
            port_len = j;
            goto done;
        }

        if (c < '0' || c > '9') {
            break;
        }
    }

    host = server->data;
    host_len = server->len;
    port = NULL;
    port_len = 0;

    done:

        ngx_crc32_init(base_hash);
        ngx_crc32_update(&base_hash, host, host_len);
        ngx_crc32_update(&base_hash, (u_char *) "", 1);
        ngx_crc32_update(&base_hash, port, port_len);

        prev_hash.value = 0;
        npoints = peer->weight * 160;

        for(j = 0; j < npoints; j++) {
            hash = base_hash;

            ngx_crc32_update(&hash, prev_hash.byte, 4);
            ngx_crc32_final(hash);

            points->point[points->number].hash = hash;
            points->point[points->number].server = server;
            points->number++;
#if (NGX_HAVE_LITTLE_ENDIAN)
            prev_hash.value = hash;
#else
            prev_hash.byte[0] = (u_char) (hash & 0xff);
            prev_hash.byte[1] = (u_char) ((hash >> 8) & 0xff);
            prev_hash.byte[2] = (u_char) ((hash >> 16) & 0xff);
            prev_hash.byte[3] = (u_char) ((hash >> 24) & 0xff);
#endif
        }
}


static int 
ngx_http_lua_upstream_consistent_hash_init(lua_State * L, 
                                ngx_http_upstream_srv_conf_t *uscf)
{
    ngx_uint_t                                npoints, i, j;
    ngx_http_upstream_chash_points_t         *points;
    ngx_http_upstream_hash_srv_conf_t        *hcf;
    size_t                                    old_size, new_size;
    ngx_http_upstream_rr_peer_t              *peer;
    ngx_http_upstream_rr_peers_t             *peers;

    hcf = ngx_http_conf_upstream_srv_conf(uscf, ngx_http_upstream_hash_module);
    if(hcf->points == NULL) {
        return 0;    
    }

    peers = uscf->peer.data;
    npoints = (peers->total_weight - peers->peer[peers->number - 1].weight) * 160;
    old_size = sizeof(ngx_http_upstream_chash_points_t)
               + sizeof(ngx_http_upstream_chash_point_t) * (npoints - 1);
    new_size = old_size
               + sizeof(ngx_http_upstream_chash_point_t) * peers->peer[peers->number - 1].weight * 160;

    points = ngx_prealloc(ngx_cycle->pool, hcf->points, old_size, new_size, 0);
    if (points == NULL ) {
        lua_pushnil(L);
        lua_pushliteral(L, "points prealloc fail\n");
        return 2;
    }

    hcf->points = points;
    peer = &peers->peer[peers->number - 1];

    ngx_http_upstream_consistent_hash(peer, points);

    ngx_qsort(points->point,
              points->number,
              sizeof(ngx_http_upstream_chash_point_t),
              ngx_http_upstream_chash_cmp_points);

    for (i = 0, j = 1; j < points->number; j++) {
        if (points->point[i].hash != points->point[j].hash) {
                points->point[++i] = points->point[j];
        }
    }

    points->number = i + 1;
    
    return 0;
}


static int
ngx_http_lua_upstream_remove_peer_chash(lua_State * L, 
                                 ngx_http_upstream_srv_conf_t *us)
{
    ngx_uint_t                                npoints, i, j;
    ngx_http_upstream_rr_peer_t              *peer;
    ngx_http_upstream_rr_peers_t             *peers;
    ngx_http_upstream_chash_points_t         *points;
    ngx_http_upstream_hash_srv_conf_t        *hcf;    
    size_t                                    size;


    hcf = ngx_http_conf_upstream_srv_conf(us, ngx_http_upstream_hash_module);
    if(hcf->points == NULL) {
        return 0;    
    }

    ngx_pfree(ngx_cycle->pool, hcf->points);

    peers = us->peer.data;
    npoints = peers->total_weight * 160;

    size = sizeof(ngx_http_upstream_chash_points_t)
           + sizeof(ngx_http_upstream_chash_point_t) * (npoints - 1); 

    points = ngx_palloc(ngx_cycle->pool, size);
    if (points == NULL) {
        lua_pushnil(L);
        lua_pushliteral(L, "points palloc fail\n");       
        return 2;
    }   

    points->number = 0;
  
    for (i = 0; i < peers->number; i++) {
        peer = &peers->peer[i];
        ngx_http_upstream_consistent_hash(peer, points);

    }

    ngx_qsort(points->point,
              points->number,
              sizeof(ngx_http_upstream_chash_point_t),
              ngx_http_upstream_chash_cmp_points);

    for (i = 0, j = 1; j < points->number; j++) {
        if (points->point[i].hash != points->point[j].hash) {
            points->point[++i] = points->point[j];
        }
    }

    points->number = i + 1;

    hcf->points = points;
    
    return 0;
}

#endif


#if (NGX_HTTP_UPSTREAM_LEAST_CONN)

static int 
ngx_http_lua_upstream_least_conn_init(lua_State * L, 
                     ngx_http_upstream_srv_conf_t *uscf, ngx_uint_t flag)
{
    ngx_http_upstream_rr_peers_t          *peers;
    ngx_http_upstream_least_conn_conf_t   *lcf;
    ngx_uint_t                            *conns, n;
    size_t                                 old_size, new_size;


    lcf = ngx_http_conf_upstream_srv_conf(uscf,
                                              ngx_http_upstream_least_conn_module);
    if(lcf->conns == NULL) {
        return 0;   
    }

    peers = uscf->peer.data;
    n = peers->number;
    n += peers->next ? peers->next->number : 0;
    new_size = sizeof(ngx_uint_t) * n;
    old_size = new_size - sizeof(ngx_uint_t);

    conns = ngx_prealloc(ngx_cycle->pool, lcf->conns, old_size, new_size, 0);
    if (conns == NULL ) {
        lua_pushnil(L);
        lua_pushliteral(L, "conns prealloc fail\n");
        return 2;
    }

    n = flag ? peers->number : peers->next->number;
    conns[n - 1] = 0;
    lcf->conns = conns;
   
    return 0;
}


static int
ngx_http_lua_upstream_remover_peer_least_conn(lua_State * L,
                                 ngx_http_upstream_srv_conf_t *us, ngx_uint_t pos)
{
    ngx_http_upstream_least_conn_conf_t     *lcf;
    ngx_uint_t                              *conns;
    ngx_http_upstream_rr_peers_t            *peers;
    ngx_uint_t                               i, n;
    size_t                                   old_size, new_size;


    peers = us->peer.data;

    lcf = ngx_http_conf_upstream_srv_conf(us,
                                              ngx_http_upstream_least_conn_module);

    if(lcf->conns == NULL) {
        return 0;   
    }

    for (i = pos; i < peers->number; i++) {
        lcf->conns[i] = lcf->conns[i + 1];
    }

    n = peers->number;
    n += peers->next ? peers->next->number : 0;
    new_size = sizeof(ngx_uint_t) * n;
    old_size = new_size + sizeof(ngx_uint_t);

    conns = ngx_prealloc(ngx_cycle->pool, lcf->conns, old_size, new_size, 0);
    if (conns == NULL) {
        lua_pushnil(L);
        lua_pushliteral(L, "conns realloc fail");
        return 2;
    }

    lcf->conns = conns;

    return 0;
}


#endif

/*
 * The function is add a server to back-end peers 
 * it's suitable for ip_hash round_robin least_conn,
 * hash the peer's weight ip port ... depends on
 * nginx.conf.   
*/
static int
ngx_http_lua_upstream_add_peer(lua_State * L)
{
    ngx_uint_t                             n;
    ngx_http_upstream_server_t            *us;
    ngx_http_upstream_srv_conf_t          *uscf;
    ngx_http_upstream_rr_peer_t            peer; 
    ngx_http_upstream_rr_peers_t          *peers; 
    ngx_http_upstream_rr_peers_t          *backup; 
    ngx_http_request_t                    *r;
    ngx_url_t                              u;
    size_t                                 old_size, new_size;
#if (NGX_HTTP_UPSTREAM_LEAST_CONN) 
    ngx_uint_t                             flag;

    flag = 0;
#endif

    if (lua_gettop(L) != 2) {
        // two param is :  "upstream" "ip:port" 
        // for lua code , you must pass this one param, is none ,you should 
        // consider pass default value.
        lua_pushnil(L);
        lua_pushliteral(L, "exactly two argument expected\n");
        return 2;
    }

    r = ngx_http_lua_get_request(L);
    if (r == NULL) {
        lua_pushnil(L);
        lua_pushliteral(L, "get request error \n");
        return 2;
    }

    ngx_memzero(&u, sizeof (ngx_url_t));
    u.url.data = (u_char *) luaL_checklstring(L, 2, &u.url.len);
    u.default_port = 80;

#if (NGX_DEBUG)
    ngx_log_error(NGX_LOG_ALERT, r->connection->log, 0, "%s %s params: %s\n", __FILE__,__FUNCTION__ , u.url.data );
#endif

    uscf = ngx_http_lua_upstream_check_peers(L, u, &us);
    if ( uscf == NULL || us == NULL) {
       return 2;
    }
    
    peers = uscf->peer.data;

    ngx_memzero(&peer, sizeof (ngx_http_upstream_rr_peer_t));

    if ( !us->backup ) {
        if (ngx_http_lua_upstream_exist_peer(peers, u) == 1) {
            lua_pushnil(L);
            lua_pushliteral(L, "the peer is exist\n");
            return 2;
        }

        n = peers->number - 1;
        n += peers->next != NULL ? peers->next->number : 0;
        old_size = n * sizeof(ngx_http_upstream_rr_peer_t) 
			+ sizeof(ngx_http_upstream_rr_peers_t) * (peers->next != NULL ? 2 : 1);
        new_size = old_size + sizeof(ngx_http_upstream_rr_peer_t);
    
        peers = ngx_prealloc(ngx_cycle->pool, uscf->peer.data, old_size, new_size, 1);
        if (peers == NULL) {
            lua_pushnil(L);
            lua_pushliteral(L, "peers pcalloc fail\n");
            return 2;
        }

        peer.weight = us->weight; 
        peer.effective_weight = us->weight;
        peer.current_weight= 0;
        peer.max_fails = us->max_fails;
        peer.fail_timeout = us->fail_timeout;
        peer.sockaddr = us->addrs->sockaddr;
        peer.socklen = us->addrs->socklen;
        peer.name = us->addrs->name;
        peer.down = us->down;
        peer.fails = 0;
        peer.server = us->name;

        peers->peer[peers->number++] = peer;
        peers->total_weight += peer.weight;
        peers->single = (peers->number == 1);
        peers->weighted = (peers->total_weight != peers->number);

#if (NGX_HTTP_UPSTREAM_LEAST_CONN) 
        flag = 1;
#endif

    } else {
        backup = peers->next;
        if (ngx_http_lua_upstream_exist_peer(backup, u) == 1) {
            lua_pushnil(L);
            lua_pushliteral(L, "the backup peer is exist\n");
            return 2;
        }

        n = backup != NULL ? (backup->number - 1) : 0;

        old_size = n * sizeof(ngx_http_upstream_rr_peer_t) 
			       + sizeof(ngx_http_upstream_rr_peers_t);
        new_size = sizeof(ngx_http_upstream_rr_peer_t) + old_size;
    
        backup  = ngx_prealloc(ngx_cycle->pool, peers->next, old_size, new_size, 1);
        if (backup == NULL ) {
            lua_pushnil(L);
            lua_pushliteral(L, "backup pcalloc fail\n");
            return 2;
        }

        peers->single = 0;
        backup->single = 0;

        peer.weight = us->weight; 
        peer.effective_weight = us->weight;
        peer.current_weight= 0;
        peer.max_fails = us->max_fails;
        peer.fail_timeout = us->fail_timeout;
        peer.server = us->name;
        peer.sockaddr = us->addrs->sockaddr;
        peer.socklen = us->addrs->socklen;
        peer.name = us->addrs->name;
        peer.down = us->down;
        peer.fails = 0;

        backup->peer[backup->number++] = peer;
        backup->total_weight += peer.weight;
        backup->single = (backup->number == 1);
        backup->weighted = (backup->total_weight != backup->number);

        peers->next = backup;
    }
   
    uscf->peer.data = peers;

#if (NGX_HTTP_UPSTREAM_LEAST_CONN) 
    if(ngx_http_lua_upstream_least_conn_init(L, uscf, flag)) {
        return 2;
    }
#endif

#if (NGX_HTTP_UPSTREAM_CONSISTENT_HASH)
    if(ngx_http_lua_upstream_consistent_hash_init(L, uscf)) {
        return 2;
    }
#endif

    lua_pushboolean(L, 1);
    return 1;
}


/*
 * The function is remove server from back-end peers. if 
 * the server is not find and return error and notes the 
 * server is not find. now suitable for round_robin or
 * ip_hash least_conn hash. 
*/
static int
ngx_http_lua_upstream_remove_peer(lua_State * L)
{
    ngx_uint_t                               i, j, n;
    size_t                                   len;
    ngx_str_t                                host;
    ngx_http_upstream_rr_peers_t            *peers; 
    ngx_http_upstream_srv_conf_t            *uscf;
    ngx_http_request_t                      *r;
    ngx_url_t                                u;
    size_t                                   old_size, new_size;

    if (lua_gettop(L) != 2) {
        // two param is :  "bar","ip:port" 
        // for lua code , you must pass this two param, is none ,you should 
        // consider pass default value.
        lua_pushnil(L);
        lua_pushliteral(L, "exactly two argument expected\n");
        return 2;
    }

    r = ngx_http_lua_get_request(L);
    if (r == NULL) {
        lua_pushnil(L);
        lua_pushliteral(L, "get request error \n");
        return 2;
    }

    host.data = (u_char *) luaL_checklstring(L, 1, &host.len);

    ngx_memzero(&u, sizeof (ngx_url_t));
    u.url.data = (u_char *) luaL_checklstring(L, 2, &u.url.len);
    u.default_port = 80;

#if (NGX_DEBUG)
    ngx_log_error(NGX_LOG_ALERT, r->connection->log, 0, "%s %s params: %s\n", __FILE__,__FUNCTION__ , u.url.data );
#endif

    uscf = ngx_http_lua_upstream_find_upstream(L, &host);
    if (uscf == NULL) {
        lua_pushnil(L);
        lua_pushliteral(L, "upstream not found\n");
        return 2;
    }
    
    peers = uscf->peer.data;    
    if (peers == NULL ) {
        lua_pushnil(L);
        lua_pushliteral(L, "peers is null\n");
        return 2;
    }

    if (peers->number == 1) {
        lua_pushnil(L);
        lua_pushliteral(L, "upstream last one is not allowed to delete\n");
        return 2;
    }

    if (ngx_http_lua_upstream_exist_peer(peers, u) == 0) {
        lua_pushnil(L);
        lua_pushliteral(L, "not found this peer\n");
        return 2;
    }

    for (i = 0; (peers->peer != NULL) && (i < peers->number); i++) {

        len = peers->peer[i].name.len;
        if (len == u.url.len
             && ngx_memcmp(u.url.data, peers->peer[i].name.data, u.url.len) == 0) {

             for (j = i; j < peers->number - 1; j++) {
                peers->peer[j] = peers->peer[j + 1];
             }

             n = peers->number - 1;
             n += peers->next != NULL ? peers->next->number : 0;
             old_size = n * sizeof(ngx_http_upstream_rr_peer_t) 
			  + sizeof(ngx_http_upstream_rr_peers_t) * (peers->next != NULL ? 2 : 1);
             new_size = old_size - sizeof(ngx_http_upstream_rr_peer_t);

             peers  = ngx_prealloc(ngx_cycle->pool, peers, old_size, new_size, 0);

             peers->number -= 1;
             uscf->peer.data = peers;

             break;
        }
    }

#if (NGX_HTTP_UPSTREAM_LEAST_CONN)
    if (ngx_http_lua_upstream_remover_peer_least_conn(L, uscf, i)) {
        return 2;
    }

#endif

#if (NGX_HTTP_UPSTREAM_CONSISTENT_HASH)
    if (ngx_http_lua_upstream_remove_peer_chash(L, uscf)) {
        return 2;
    }

#endif

    lua_pushboolean(L, 1);
    return 1;
}


/*
 * The function is check upstream whether there is  
 * such a u.url.data,if exist return srv_conf_t structure 
 * else return NULL.
*/
static ngx_http_upstream_srv_conf_t *
ngx_http_lua_upstream_check_peers(lua_State * L, ngx_url_t u, 
                                              ngx_http_upstream_server_t ** srv)
{
    ngx_http_upstream_srv_conf_t               *uscf;
    ngx_str_t                                   host;

    if (lua_gettop(L) != 2) {
        lua_pushnil(L);
        lua_pushliteral(L, "no argument expected\n");
        return NULL;
    }

    ngx_memzero(&host, sizeof(ngx_str_t));
    host.data = (u_char *) luaL_checklstring(L, 1, &host.len);

    uscf = ngx_http_lua_upstream_find_upstream(L, &host);
    if (uscf == NULL) {
        lua_pushnil(L);
        lua_pushliteral(L, "upstream not found\n");
        return NULL;
    }

   *srv = ngx_http_lua_upstream_compare_server(uscf, u);
   if (*srv == NULL) {    
        lua_pushnil(L);
        lua_pushliteral(L,"not find this peer\n");
        return NULL;
    }

    return uscf;
}


/*
 * The function is check current peers whether exists 
 * a peer  such a u.url.data if exists and return 1,
 * else return 0.
*/
static int
ngx_http_lua_upstream_exist_peer(ngx_http_upstream_rr_peers_t * peers, ngx_url_t u)
{
    ngx_uint_t                          i;
    size_t                              len;
    ngx_http_upstream_rr_peer_t         peer;

    for (i = 0; (peers != NULL) && (i < peers->number); i++) {
        peer = peers->peer[i];

        len = peer.name.len;
        if (len == u.url.len
            && ngx_memcmp(u.url.data, peer.name.data, u.url.len) == 0) {
            return 1;
        }
    }
    
    return 0;
}


static ngx_int_t
ngx_pfree_and_delay(ngx_pool_t *pool, void *p)    
{
    ngx_pool_large_t  *l;

    for (l = pool->large; l; l = l->next) {
        if (p == l->alloc) {
            ngx_log_debug1(NGX_LOG_DEBUG_ALLOC, pool->log, 0,
                           "delay free: %p", l->alloc);

            ngx_http_lua_upstream_event_init(l->alloc); 

            return NGX_OK;
        }
    }

    return NGX_DECLINED;
}


/*
 * The function copy from tengine-2.1.0 core/ngx_palloc.c. 
 *
*/
static void *
ngx_prealloc(ngx_pool_t *pool, void *p, size_t old_size, size_t new_size, ngx_uint_t flag)  
{
    void                *new;
    ngx_pool_t          *node;

    if (p == NULL) {
        return ngx_palloc(pool, new_size);
    }    

    if (new_size == 0) { 
        if ((u_char *) p + old_size == pool->d.last) {
           pool->d.last = p; 

        } else {
           ngx_pfree(pool, p);  
        }

        return NULL;
    }    

    if (old_size <= pool->max) {
        for (node = pool; node; node = node->d.next) {
            if ((u_char *)p + old_size == node->d.last
                && (u_char *)p + new_size <= node->d.end) {
                node->d.last = (u_char *)p + new_size;
                return p;
            }
        }
    }    

    if (new_size <= old_size) {
       return p;
    }

    new = ngx_palloc(pool, new_size);
    if (new == NULL) {
        return NULL;
    }
    
    ngx_memcpy(new, p, old_size);

    if (flag) {
        ngx_pfree_and_delay(pool, p);

    } else {
        ngx_pfree(pool, p);
    }

    return new;
}


static int
ngx_http_lua_upstream_get_upstreams(lua_State * L)
{
    ngx_uint_t                            i;
    ngx_http_upstream_srv_conf_t        **uscfp, *uscf;
    ngx_http_upstream_main_conf_t        *umcf;

    if (lua_gettop(L) != 0) {
        return luaL_error(L, "no argument expected");
    }

    umcf = ngx_http_lua_upstream_get_upstream_main_conf(L);
    uscfp = umcf->upstreams.elts;

    lua_createtable(L, umcf->upstreams.nelts, 0);

    for (i = 0; i < umcf->upstreams.nelts; i++) {

        uscf = uscfp[i];

        lua_pushlstring(L, (char *) uscf->host.data, uscf->host.len);
        if (uscf->port) {
            lua_pushfstring(L, ":%d", (int) uscf->port);
            lua_concat(L, 2);

            /* XXX maybe we should also take "default_port" into account
             * here? */
        }

        lua_rawseti(L, -2, i + 1);
    }

    return 1;
}


static int
ngx_http_lua_upstream_get_servers(lua_State * L)
{
    ngx_str_t                             host;
    ngx_uint_t                            i, j, n;
    ngx_http_upstream_server_t           *server;
    ngx_http_upstream_srv_conf_t         *us;

    if (lua_gettop(L) != 1) {
        return luaL_error(L, "exactly one argument expected");
    }

    host.data = (u_char *) luaL_checklstring(L, 1, &host.len);

    us = ngx_http_lua_upstream_find_upstream(L, &host);
    if (us == NULL) {
        lua_pushnil(L);
        lua_pushliteral(L, "upstream not found");
        return 2;
    }

    if (us->servers == NULL || us->servers->nelts == 0) {
        lua_newtable(L);
        return 1;
    }

    server = us->servers->elts;

    lua_createtable(L, us->servers->nelts, 0);

    for (i = 0; i < us->servers->nelts; i++) {

        n = 4;

        if (server[i].backup) {
            n++;
        }

        if (server[i].down) {
            n++;
        }

        lua_createtable(L, 0, n);

        lua_pushliteral(L, "addr");

        if (server[i].naddrs == 1) {
            lua_pushlstring(L, (char *) server[i].addrs->name.data,
                            server[i].addrs->name.len);

        } else {
            lua_createtable(L, server[i].naddrs, 0);

            for (j = 0; j < server[i].naddrs; j++) {
                lua_pushlstring(L, (char *) server[i].addrs[j].name.data,
                                server[i].addrs[j].name.len);
                lua_rawseti(L, -2, j + 1);
            }
        }

        lua_rawset(L, -3);

        lua_pushliteral(L, "weight");
        lua_pushinteger(L, (lua_Integer) server[i].weight);
        lua_rawset(L, -3);

        lua_pushliteral(L, "max_fails");
        lua_pushinteger(L, (lua_Integer) server[i].max_fails);
        lua_rawset(L, -3);

        lua_pushliteral(L, "fail_timeout");
        lua_pushinteger(L, (lua_Integer) server[i].fail_timeout);
        lua_rawset(L, -3);

        if (server[i].backup) {
            lua_pushliteral(L, "backup");
            lua_pushboolean(L, 1);
            lua_rawset(L, -3);
        }

        if (server[i].down) {
            lua_pushliteral(L, "down");
            lua_pushboolean(L, 1);
            lua_rawset(L, -3);
        }

        lua_rawseti(L, -2, i + 1);
    }

    return 1;
}


static int
ngx_http_lua_upstream_get_primary_peers(lua_State * L)
{
    ngx_str_t                             host;
    ngx_uint_t                            i;
    ngx_http_upstream_rr_peers_t         *peers;
    ngx_http_upstream_srv_conf_t         *us;

    if (lua_gettop(L) != 1) {
        return luaL_error(L, "exactly one argument expected");
    }

    host.data = (u_char *) luaL_checklstring(L, 1, &host.len);

    us = ngx_http_lua_upstream_find_upstream(L, &host);
    if (us == NULL) {
        lua_pushnil(L);
        lua_pushliteral(L, "upstream not found");
        return 2;
    }

    peers = us->peer.data;

    if (peers == NULL) {
        lua_pushnil(L);
        lua_pushliteral(L, "no peer data");
        return 2;
    }

    lua_createtable(L, peers->number, 0);

    for (i = 0; i < peers->number; i++) {
        ngx_http_lua_get_peer(L, &peers->peer[i], i);
        lua_rawseti(L, -2, i + 1);
    }

    return 1;
}


static int
ngx_http_lua_upstream_get_backup_peers(lua_State * L)
{
    ngx_str_t                             host;
    ngx_uint_t                            i;
    ngx_http_upstream_rr_peers_t         *peers;
    ngx_http_upstream_srv_conf_t         *us;

    if (lua_gettop(L) != 1) {
        return luaL_error(L, "exactly one argument expected");
    }

    host.data = (u_char *) luaL_checklstring(L, 1, &host.len);

    us = ngx_http_lua_upstream_find_upstream(L, &host);
    if (us == NULL) {
        lua_pushnil(L);
        lua_pushliteral(L, "upstream not found");
        return 2;
    }

    peers = us->peer.data;

    if (peers == NULL) {
        lua_pushnil(L);
        lua_pushliteral(L, "no peer data");
        return 2;
    }

    peers = peers->next;
    if (peers == NULL) {
        lua_newtable(L);
        return 1;
    }

    lua_createtable(L, peers->number, 0);

    for (i = 0; i < peers->number; i++) {
        ngx_http_lua_get_peer(L, &peers->peer[i], i);
        lua_rawseti(L, -2, i + 1);
    }

    return 1;
}


static int
ngx_http_lua_upstream_set_peer_down(lua_State * L)
{
    ngx_http_upstream_rr_peer_t          *peer;

    if (lua_gettop(L) != 4) {
        return luaL_error(L, "exactly 4 arguments expected");
    }

    peer = ngx_http_lua_upstream_lookup_peer(L);
    if (peer == NULL) {
        return 2;
    }

    peer->down = lua_toboolean(L, 4);

    lua_pushboolean(L, 1);
    return 1;
}


static ngx_http_upstream_rr_peer_t *
ngx_http_lua_upstream_lookup_peer(lua_State *L)
{
    int                                   id, backup;
    ngx_str_t                             host;
    ngx_http_upstream_srv_conf_t         *us;
    ngx_http_upstream_rr_peers_t         *peers;

    host.data = (u_char *) luaL_checklstring(L, 1, &host.len);

    us = ngx_http_lua_upstream_find_upstream(L, &host);
    if (us == NULL) {
        lua_pushnil(L);
        lua_pushliteral(L, "upstream not found");
        return NULL;
    }

    peers = us->peer.data;

    if (peers == NULL) {
        lua_pushnil(L);
        lua_pushliteral(L, "no peer data");
        return NULL;
    }

    backup = lua_toboolean(L, 2);
    if (backup) {
        peers = peers->next;
    }

    if (peers == NULL) {
        lua_pushnil(L);
        lua_pushliteral(L, "no backup peers");
        return NULL;
    }

    id = luaL_checkint(L, 3);
    if (id < 0 || (ngx_uint_t) id >= peers->number) {
        lua_pushnil(L);
        lua_pushliteral(L, "bad peer id");
        return NULL;
    }

    return &peers->peer[id];
}



static int
ngx_http_lua_get_peer(lua_State *L, ngx_http_upstream_rr_peer_t *peer,
    ngx_uint_t id)
{
    ngx_uint_t     n;

    n = 8;

    if (peer->down) {
        n++;
    }

    if (peer->accessed) {
        n++;
    }

    if (peer->checked) {
        n++;
    }

    lua_createtable(L, 0, n);

    lua_pushliteral(L, "id");
    lua_pushinteger(L, (lua_Integer) id);
    lua_rawset(L, -3);

    lua_pushliteral(L, "name");
    lua_pushlstring(L, (char *) peer->name.data, peer->name.len);
    lua_rawset(L, -3);

    lua_pushliteral(L, "weight");
    lua_pushinteger(L, (lua_Integer) peer->weight);
    lua_rawset(L, -3);

    lua_pushliteral(L, "current_weight");
    lua_pushinteger(L, (lua_Integer) peer->current_weight);
    lua_rawset(L, -3);

    lua_pushliteral(L, "effective_weight");
    lua_pushinteger(L, (lua_Integer) peer->effective_weight);
    lua_rawset(L, -3);

    lua_pushliteral(L, "fails");
    lua_pushinteger(L, (lua_Integer) peer->fails);
    lua_rawset(L, -3);

    lua_pushliteral(L, "max_fails");
    lua_pushinteger(L, (lua_Integer) peer->max_fails);
    lua_rawset(L, -3);

    lua_pushliteral(L, "fail_timeout");
    lua_pushinteger(L, (lua_Integer) peer->fail_timeout);
    lua_rawset(L, -3);

    if (peer->accessed) {
        lua_pushliteral(L, "accessed");
        lua_pushinteger(L, (lua_Integer) peer->accessed);
        lua_rawset(L, -3);
    }

    if (peer->checked) {
        lua_pushliteral(L, "checked");
        lua_pushinteger(L, (lua_Integer) peer->checked);
        lua_rawset(L, -3);
    }

    if (peer->down) {
        lua_pushliteral(L, "down");
        lua_pushboolean(L, 1);
        lua_rawset(L, -3);
    }

    return 0;
}

static ngx_http_upstream_main_conf_t *
ngx_http_lua_upstream_get_upstream_main_conf(lua_State *L)
{
    ngx_http_request_t                   *r;

    r = ngx_http_lua_get_request(L);

    if (r == NULL) {
        return ngx_http_cycle_get_module_main_conf(ngx_cycle,
                                                   ngx_http_upstream_module);
    }

    return ngx_http_get_module_main_conf(r, ngx_http_upstream_module);
}


static ngx_http_upstream_srv_conf_t *
ngx_http_lua_upstream_find_upstream(lua_State *L, ngx_str_t *host)
{
    u_char                               *port;
    size_t                                len;
    ngx_int_t                             n;
    ngx_uint_t                            i;
    ngx_http_upstream_srv_conf_t        **uscfp, *uscf;
    ngx_http_upstream_main_conf_t        *umcf;

    umcf = ngx_http_lua_upstream_get_upstream_main_conf(L);
    uscfp = umcf->upstreams.elts;

    for (i = 0; i < umcf->upstreams.nelts; i++) {

        uscf = uscfp[i];

        if (uscf->host.len == host->len
            && ngx_memcmp(uscf->host.data, host->data, host->len) == 0) {
            return uscf;
        }
    }

    port = ngx_strlchr(host->data, host->data + host->len, ':');
    if (port) {
        port++;
        n = ngx_atoi(port, host->data + host->len - port);
        if (n < 1 || n > 65535) {
            return NULL;
        }

        /* try harder with port */

        len = port - host->data - 1;

        for (i = 0; i < umcf->upstreams.nelts; i++) {

            uscf = uscfp[i];

            if (uscf->port
                && uscf->port == n
                && uscf->host.len == len
                && ngx_memcmp(uscf->host.data, host->data, len) == 0) {
                return uscf;
            }
        }
    }

    return NULL;
}

