#ifndef NGX_LUA_HTTP_MODULE
#define NGX_LUA_HTTP_MODULE


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

static int
ngx_http_lua_upstream_add_server(lua_State * L);

static ngx_http_upstream_server_t*
ngx_http_lua_upstream_compare_server(ngx_http_upstream_srv_conf_t * us , ngx_url_t u);

static ngx_http_upstream_srv_conf_t *
ngx_http_lua_upstream_check_peers(lua_State * L,ngx_url_t u,ngx_http_upstream_server_t ** srv);

static int
ngx_http_lua_upstream_exist_peer(ngx_http_upstream_rr_peers_t * peers , ngx_url_t u);

static int
ngx_http_lua_upstream_add_peer(lua_State * L);

static int
ngx_http_lua_upstream_remove_server(lua_State * L);

static int
ngx_http_lua_upstream_remove_peer(lua_State * L);


void *ngx_prealloc(ngx_pool_t *pool, void *p, size_t old_size, size_t new_size);


#if (NGX_HTTP_UPSTREAM_CHECK)
ngx_uint_t 
ngx_http_lua_upstream_add_check_peer(ngx_http_upstream_srv_conf_t *us , ngx_addr_t *peer_addr);


//Mark the follow function from ngx_http_check_module.c file,
//i'll submit a patch to delete static of ngx_http_upstream_check_addr_change_port for check_module 
static ngx_int_t
ngx_http_upstream_check_addr_change_port(ngx_pool_t *pool, ngx_addr_t *dst,
                                                             ngx_addr_t *src, ngx_uint_t port);
#endif


//Mark the follow structure from ngx_http_check_module.c file,
//i'll submit a patch to add a header file for check_module 

#if (NGX_HTTP_UPSTREAM_CHECK)

//Mark the variable from ngx_http_upstream_check_module.c file 
extern ngx_module_t ngx_http_upstream_check_module;

typedef struct ngx_http_upstream_check_peer_s ngx_http_upstream_check_peer_t;
typedef struct ngx_http_upstream_check_srv_conf_s
    ngx_http_upstream_check_srv_conf_t;

typedef ngx_int_t (*ngx_http_upstream_check_packet_init_pt)
    (ngx_http_upstream_check_peer_t *peer);
typedef ngx_int_t (*ngx_http_upstream_check_packet_parse_pt)
    (ngx_http_upstream_check_peer_t *peer);
typedef void (*ngx_http_upstream_check_packet_clean_pt)
    (ngx_http_upstream_check_peer_t *peer);


typedef struct {
    ngx_shmtx_t                              mutex;
    ngx_shmtx_sh_t                           lock;

    ngx_pid_t                                owner;

    ngx_msec_t                               access_time;

    ngx_uint_t                               fall_count;
    ngx_uint_t                               rise_count;

    ngx_uint_t                               busyness;
    ngx_uint_t                               access_count;

    struct sockaddr                         *sockaddr;
    socklen_t                                socklen;

    ngx_atomic_t                             down;

    u_char                                   padding[64];
} ngx_http_upstream_check_peer_shm_t;


typedef struct {
    ngx_uint_t                               generation;
    ngx_uint_t                               checksum;
    ngx_uint_t                               number;

    /* ngx_http_upstream_check_status_peer_t */
    ngx_http_upstream_check_peer_shm_t       peers[1];
} ngx_http_upstream_check_peers_shm_t;


struct ngx_http_upstream_check_peer_s {
    ngx_flag_t                               state;
    ngx_pool_t                              *pool;
    ngx_uint_t                               index;
    ngx_uint_t                               max_busy;
    ngx_str_t                               *upstream_name;
    ngx_addr_t                              *check_peer_addr;
    ngx_addr_t                              *peer_addr;
    ngx_event_t                              check_ev;
    ngx_event_t                              check_timeout_ev;
    ngx_peer_connection_t                    pc;

    void                                    *check_data;
    ngx_event_handler_pt                     send_handler;
    ngx_event_handler_pt                     recv_handler;

    ngx_http_upstream_check_packet_init_pt   init;

    ngx_http_upstream_check_packet_parse_pt  parse;
    ngx_http_upstream_check_packet_clean_pt  reinit;

    ngx_http_upstream_check_peer_shm_t      *shm;
    ngx_http_upstream_check_srv_conf_t      *conf;
};

typedef struct {
    ngx_str_t                                check_shm_name;
    ngx_uint_t                               checksum;
    ngx_array_t                              peers;

    ngx_http_upstream_check_peers_shm_t     *peers_shm;
} ngx_http_upstream_check_peers_t;

typedef struct {
    ngx_uint_t                               type;

    ngx_str_t                                name;

    ngx_str_t                                default_send;

    /* HTTP */
    ngx_uint_t                               default_status_alive;

    ngx_event_handler_pt                     send_handler;
    ngx_event_handler_pt                     recv_handler;

    ngx_http_upstream_check_packet_init_pt   init;
    ngx_http_upstream_check_packet_parse_pt  parse;
    ngx_http_upstream_check_packet_clean_pt  reinit;

    unsigned need_pool;
    unsigned need_keepalive;
} ngx_check_conf_t;



struct ngx_http_upstream_check_srv_conf_s {
    ngx_uint_t                               port;
    ngx_uint_t                               fall_count;
    ngx_uint_t                               rise_count;
    ngx_msec_t                               check_interval;
    ngx_msec_t                               check_timeout;
    ngx_uint_t                               check_keepalive_requests;

    ngx_check_conf_t                        *check_type_conf;
    ngx_str_t                                send;

    union {
        ngx_uint_t                           return_code;
        ngx_uint_t                           status_alive;
    } code;

    ngx_array_t                             *fastcgi_params;

    ngx_uint_t                               default_down;
};

typedef struct {
    ngx_uint_t                               check_shm_size;
    ngx_http_upstream_check_peers_t         *peers;
} ngx_http_upstream_check_main_conf_t;


#endif


#endif
