#ifndef NGX_LUA_HTTP_MODULE
#define NGX_LUA_HTTP_MODULE

// the NGX_HTTP_UPSTREAM_LEAST_CONN define is support least_conn.  
#define NGX_HTTP_UPSTREAM_LEAST_CONN        0
// the NGX_HTTP_UPSTREAM_LEAST_CONN define is support hash.
#define NGX_HTTP_UPSTREAM_CONSISTENT_HASH   0


#if (NGX_HTTP_UPSTREAM_LEAST_CONN)

//Mark the variable from ngx_http_upstream_least_conn_module.c file 
extern ngx_module_t ngx_http_upstream_least_conn_module;


//Mark the struct from ngx_http_upstream_least_conn_module.c file 
typedef struct {
    ngx_uint_t                        *conns;
} ngx_http_upstream_least_conn_conf_t;

#endif


#if (NGX_HTTP_UPSTREAM_CONSISTENT_HASH)

extern  ngx_module_t ngx_http_upstream_hash_module;


typedef struct {
    uint32_t                            hash;
    ngx_str_t                          *server;
} ngx_http_upstream_chash_point_t;


typedef struct {
    ngx_uint_t                          number;
    ngx_http_upstream_chash_point_t     point[1];
} ngx_http_upstream_chash_points_t;


typedef struct {
    ngx_http_complex_value_t            key;
    ngx_http_upstream_chash_points_t   *points;
} ngx_http_upstream_hash_srv_conf_t;


#endif

#endif
