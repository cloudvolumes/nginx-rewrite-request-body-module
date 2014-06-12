#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>

ngx_flag_t ngx_http_rewrite_request_body_used = 0;

typedef struct {
    ngx_flag_t          done:1;
    ngx_flag_t          waiting_more_body:1;
} ngx_http_rewrite_request_body_ctx_t;

static char *ngx_http_rewrite_request_body_conf_handler(ngx_conf_t *cf,
    ngx_command_t *cmd, void *conf);

static ngx_int_t ngx_http_rewrite_request_body_init(ngx_conf_t *cf);
static ngx_int_t ngx_http_rewrite_request_body_handler(ngx_http_request_t *r);
static void ngx_http_rewrite_request_body_post_read(ngx_http_request_t *r);

static ngx_command_t ngx_http_rewrite_request_body_commands[] = {

    { ngx_string("rewrite_request_body"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_NOARGS,
      ngx_http_rewrite_request_body_conf_handler,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      NULL },

      ngx_null_command
};

static ngx_http_module_t ngx_http_rewrite_request_body_module_ctx = {
    NULL,                                   /* preconfiguration */
    ngx_http_rewrite_request_body_init,               /* postconfiguration */

    NULL,                                   /* create main configuration */
    NULL,                                   /* init main configuration */

    NULL,                                   /* create server configuration */
    NULL,                                   /* merge server configuration */

    NULL,                                   /* create location configuration */
    NULL                                   /* merge location configuration */
};

ngx_module_t ngx_http_rewrite_request_body_module = {
    NGX_MODULE_V1,
    &ngx_http_rewrite_request_body_module_ctx,        /* module context */
    ngx_http_rewrite_request_body_commands,           /* module directives */
    NGX_HTTP_MODULE,                        /* module type */
    NULL,                                   /* init master */
    NULL,                                   /* init module */
    NULL,                                   /* init process */
    NULL,                                   /* init thread */
    NULL,                                   /* exit thread */
    NULL,                                   /* exit precess */
    NULL,                                   /* exit master */
    NGX_MODULE_V1_PADDING
};

static char *
ngx_http_rewrite_request_body_conf_handler(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf)
{
    ngx_http_rewrite_request_body_used = 1;
    //printf(__FUNCTION__ ": called\n");
    return NGX_CONF_OK;
}

/* register a new rewrite phase handler */
static ngx_int_t
ngx_http_rewrite_request_body_init(ngx_conf_t *cf)
{

    ngx_http_handler_pt             *h;
    ngx_http_core_main_conf_t       *cmcf;

    //printf(__FUNCTION__ ": called ngx_http_rewrite_request_body_used=%d\n", ngx_http_rewrite_request_body_used);

    if (!ngx_http_rewrite_request_body_used) {
        return NGX_OK;
    }

    cmcf = ngx_http_conf_get_module_main_conf(cf, ngx_http_core_module);

    h = ngx_array_push(&cmcf->phases[NGX_HTTP_REWRITE_PHASE].handlers);

    if (h == NULL) {
        return NGX_ERROR;
    }

    *h = ngx_http_rewrite_request_body_handler;

    return NGX_OK;
}

/* an rewrite phase handler */
static ngx_int_t
ngx_http_rewrite_request_body_handler(ngx_http_request_t *r)
{
    ngx_http_rewrite_request_body_ctx_t       *ctx;
    ngx_int_t                        rc;

//  ngx_log_debug3(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
//                "%s:%d: method %d  ... entered", __FUNCTION__, __LINE__, r->method);

    ctx = ngx_http_get_module_ctx(r, ngx_http_rewrite_request_body_module);

//  ngx_log_debug3(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
//                "%s:%d: method %d", __FUNCTION__, __LINE__, r->method);

    if (ctx != NULL) {
        if (ctx->done) {
            ngx_log_debug2(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                           "%s:%d: returning declined", __FUNCTION__, __LINE__);

            return NGX_DECLINED;
        }

        ngx_log_debug2(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                       "%s:%d: returning done", __FUNCTION__, __LINE__);
        return NGX_DONE;
    }

    if (r->method != NGX_HTTP_POST && r->method != NGX_HTTP_PUT)
    {
        ngx_log_debug3(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                       "%s:%d: ignoring method %d (not post or put)", __FUNCTION__, __LINE__, r->method);
        return NGX_DECLINED;
    }

    if (r->headers_in.content_type == NULL ||
            r->headers_in.content_type->value.data == NULL)
    {
        ngx_log_debug3(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                       "%s:%d: ignoring method %d no content", __FUNCTION__, __LINE__, r->method);
        return NGX_DECLINED;
    }


    ctx = ngx_pcalloc(r->pool, sizeof(ngx_http_rewrite_request_body_ctx_t));
    if (ctx == NULL) {
        ngx_log_debug3(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                       "%s:%d: error %d cannot allocate", __FUNCTION__, __LINE__, r->method);
        return NGX_ERROR;
    }

    ngx_http_set_ctx(r, ctx, ngx_http_rewrite_request_body_module);

    ngx_log_debug2(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                   "%s:%d: start to read client request body", __FUNCTION__, __LINE__);

    rc = ngx_http_read_client_request_body(r, ngx_http_rewrite_request_body_post_read);

    if (rc == NGX_ERROR || rc >= NGX_HTTP_SPECIAL_RESPONSE) {
        ngx_log_debug2(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                       "%s:%d: error response", __FUNCTION__, __LINE__);
        return rc;
    }


    if (rc == NGX_AGAIN) {
        ctx->waiting_more_body = 1;

        ngx_log_debug2(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                       "%s:%d: waiting more body set", __FUNCTION__, __LINE__);
    }
    else
    {
        ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                       __FUNCTION__ ": has read the request body in one run. rc %d", rc);


        ctx->done = 1;    

        ngx_http_finalize_request(r, NGX_OK);
        rc = NGX_DECLINED;
    }

    ngx_log_debug6(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                   __FUNCTION__ ": phase %d refcount %d subrequests %d blocked %d done %d eof %d", r->phase_handler, r->count, r->subrequests, r->blocked, r->done, r->connection->read->eof);
  
    return rc;
}

static void ngx_http_rewrite_request_body_post_read(ngx_http_request_t *r)
{
    ngx_http_rewrite_request_body_ctx_t       *ctx;
    ctx = ngx_http_get_module_ctx(r, ngx_http_rewrite_request_body_module);

    ngx_log_debug6(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                   __FUNCTION__ ": phase %d refcount %d subrequests %d blocked %d done %d eof %d", r->phase_handler, r->count, r->subrequests, r->blocked, r->done, r->connection->read->eof);

    if (ctx && ctx->waiting_more_body)
    {
        ctx->waiting_more_body = 0;

        ngx_log_debug2(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                       __FUNCTION__ ": received full body r %p main %p", r, r->main);
                      
        ctx->done = 1;

        r->phase_handler--;
        ngx_http_finalize_request(r, NGX_DECLINED);
    }
}
