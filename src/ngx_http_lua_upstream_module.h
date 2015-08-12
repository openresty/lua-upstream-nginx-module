#ifndef NGX_LUA_HTTP_MODULE
#define NGX_LUA_HTTP_MODULE

// the NGX_HTTP_UPSTREAM_LEAST_CONN define is support least_conn.  
#define NGX_HTTP_UPSTREAM_LEAST_CONN 1


#if (NGX_HTTP_UPSTREAM_LEAST_CONN)

//Mark the variable from ngx_http_upstream_least_conn_module.c file 
extern ngx_module_t ngx_http_upstream_least_conn_module;


//Mark the struct from ngx_http_upstream_least_conn_module.c file 
typedef struct {
    ngx_uint_t                        *conns;
} ngx_http_upstream_least_conn_conf_t;

#endif

#endif
