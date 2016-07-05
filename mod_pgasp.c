/*
 * mod_pgasp.c - Apache module for PGASP (Adaptive Server Pages for Postgres)
 * Authors: "Alex Nedoboi" <my seven-letter surname at gmail>
 *          "Maxim Zakharov" <dp.maxime@gmail.com>
 *
 * See pgasp.org for documentation
 *
 * Compilation: apxs -i -a -c -I /usr/include/postgresql -l pq mod_pgasp.c
 *
 * To register and enable in /etc/apache2/apache2.conf:
 *
 *    AddHandler pgasp-handler .pgasp
 *    pgaspEnabled On
 *    pgaspConnectionString "host=... dbname=... user=... password=..."
 *
 * 2014-12-30 Started
 * 2015-01-02 Module is working, now onto Postgres connection
 * 2015-01-05 Postgres connection done
 * 2015-01-06 Added allowed requests, reading from .conf
 * 2015-01-07 Added clean_up_connection()
 * 2015-01-08 Added spit_pg_error()
 * 2015-01-09 Reading connection string from .conf now
 * 2015-01-17 Now passing GET to PL/pgSQL function as text parameter
 *
 * TODO: Pass POST to the PL/pgSQL function
 * TODO: Write helper PL/pgSQL functions to parse POST
 * TODO: Think of pgaspAllowedPage and pgaspAllowedFunction in .conf (instead of just pgaspAllowed)
 *
 */

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <libpq-fe.h>

#include "httpd.h"
#include "http_core.h"
#include "http_config.h"
#include "http_log.h"
#include "http_protocol.h"
#include "http_request.h"
#include "apr.h"
#include "apr_reslist.h"
#include "apr_strings.h"
#include "apr_escape.h"
#include "util_script.h"

#define spit_pg_error(st) { ap_rprintf(r,"<!-- "); ap_rprintf(r,"Cannot %s: %s\n",st,PQerrorMessage(pgc)); ap_rprintf(r," -->\n"); }
#define MAX_ALLOWED_PAGES 100

#define true 1
#define false 0

/* use __(xx) macro for debug logging */
#define __(s, ...)  ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, s, __VA_ARGS__) ;

#define ISINT(val)						\
  for ( p = val; *p; ++p)					\
    if ( ! isdigit(*p) )					\
      return "Argument must be numeric!"

#define clean_up_connection(s)			\
  PQclear(pgr),					\
    pgasp_pool_close(s, pgc),			\
    OK;

typedef enum
{
  cmd_setkey, cmd_connection, cmd_allowed, cmd_enabled,
  cmd_min, cmd_keep, cmd_max, cmd_exp
}
cmd_parts ;

typedef struct
{
  const char * allowed[MAX_ALLOWED_PAGES];
  const char * connection_string;
  int connection_string_set;
  const char * key;
  int key_set;
  const char * ServerName;
  int ServerName_set;
  apr_pool_t * pool;
  apr_reslist_t* dbpool ;
  int nmin, nmin_set ;
  int nkeep, nkeep_set ;
  int nmax, nmax_set ;
  int exptime, exptime_set ;
  int is_enabled, is_enabled_set;
  int allowed_count, allowed_count_set;
}
pgasp_config;

typedef struct
{
  char *dir;
  const char *content_type;
  int content_type_set;
}
pgasp_dir_config;

typedef struct {
  request_rec *r;
  char *args;
} params_t;

PGconn* pgasp_pool_open(server_rec* s);
void pgasp_pool_close(server_rec* s, PGconn* sql);

extern module AP_MODULE_DECLARE_DATA pgasp_module ;
static apr_hash_t *pgasp_pool_config;

static int tab_args(void *data, const char *key, const char *value) {
  params_t *params = (params_t*) data;
  const char *encoded_value = apr_pescape_urlencoded(params->r->pool, value);
  if (params->args) {
    params->args = apr_pstrcat(params->r->pool, params->args, "&", key, "=", apr_pescape_urlencoded(params->r->pool, value), NULL);
  } else {
    params->args = apr_pstrcat(params->r->pool, key, "=", apr_pescape_urlencoded(params->r->pool, value), NULL);
  }
  return TRUE;/* TRUE:continue iteration. FALSE:stop iteration */
}

static const char* set_param(cmd_parms* cmd, void* cfg,
	const char* val) {
  const char* p ;
  pgasp_config* pgasp = (pgasp_config*) ap_get_module_config(cmd->server->module_config, &pgasp_module ) ;

  switch ( (intptr_t) cmd->info ) {
  case cmd_setkey:
    pgasp->key = val;
    apr_hash_set(pgasp_pool_config, pgasp->key, APR_HASH_KEY_STRING, pgasp);
    pgasp->key_set = 1;
    break ;
  case cmd_connection:
    pgasp->connection_string = val ;
    pgasp->connection_string_set = 1;
    break ;
  case cmd_allowed:
    if (pgasp->allowed_count < MAX_ALLOWED_PAGES)
      pgasp->allowed[pgasp->allowed_count++] = val;
    break;
  case cmd_min: ISINT(val) ; pgasp->nmin = atoi(val) ;
    pgasp->nmin_set = 1;
    break ;
  case cmd_keep: ISINT(val) ; pgasp->nkeep = atoi(val) ;
    pgasp->nkeep_set = 1;
    break ;
  case cmd_max: ISINT(val) ; pgasp->nmax = atoi(val) ;
    pgasp->nmax_set = 1;
    break ;
  case cmd_exp: ISINT(val) ; pgasp->exptime = atoi(val) ;
    pgasp->exptime_set = 1;
    break ;
  case cmd_enabled:
    if (!strcasecmp(val, "on")) pgasp->is_enabled = true;
    else pgasp->is_enabled = false;
    pgasp->is_enabled_set = 1;
    break;
  }
  return NULL ;
}

static const char *set_content_type(cmd_parms * cmd, void *config, const char *content_type) {
  pgasp_dir_config *conf = (pgasp_dir_config *) config;
  conf->content_type = content_type;
  conf->content_type_set = 1;
  return NULL;
}


static const command_rec pgasp_directives[] =
{
   AP_INIT_TAKE1("pgaspEnabled",          set_param, (void*)cmd_enabled, RSRC_CONF, "Enable or disable mod_pgasp"),
   AP_INIT_TAKE1("pgaspPoolKey",          set_param, (void*)cmd_setkey,     RSRC_CONF, "Unique Pool ID string"),
   AP_INIT_TAKE1("pgaspConnectionString", set_param, (void*)cmd_connection, RSRC_CONF, "PostgreSQL server connection string"),
   AP_INIT_TAKE1("pgaspAllowed",          set_param, (void*)cmd_allowed,    RSRC_CONF, "Web pages allowed to be served"),
   AP_INIT_TAKE1("pgaspPoolMin",          set_param, (void*)cmd_min,        RSRC_CONF, "Minimum number of connections"),
   AP_INIT_TAKE1("pgaspPoolKeep",         set_param, (void*)cmd_keep,       RSRC_CONF, "Maximum number of sustained connections"),
   AP_INIT_TAKE1("pgaspPoolMax",          set_param, (void*)cmd_max,        RSRC_CONF, "Maximum number of connections"),
   AP_INIT_TAKE1("pgaspPoolExptime",      set_param, (void*)cmd_exp,        RSRC_CONF, "Keepalive time for idle connections") ,
   AP_INIT_TAKE1("pgaspContentType",      set_content_type, NULL, OR_AUTHCFG, "Content-Type header to send"),
   { NULL }
};

static int pgasp_handler (request_rec * r)
{
   char cursor_string[256];
   pgasp_config* config = (pgasp_config*) ap_get_module_config(r->server->module_config, &pgasp_module ) ;
   pgasp_dir_config* dir_config = (pgasp_dir_config*) ap_get_module_config(r->per_dir_config, &pgasp_module ) ;
   apr_table_t * GET = NULL, *GETargs = NULL;
   apr_array_header_t * POST;
   PGconn * pgc;
   PGresult * pgr;
   int i, j, allowed_to_serve, filename_length = 0;
   int field_count, tuple_count;
   char * requested_file;
   char *basename;
   params_t params;

   /* PQexecParams doesn't seem to like zero-length strings, so we feed it a dummy */
   const char * dummy_get = "nothing";
   const char * dummy_user = "nobody";

   const char * cursor_values[2] = { r -> args ? apr_pstrdup(r->pool, r -> args) : dummy_get, r->user ? r->user : dummy_user };
   int cursor_value_lengths[2] = { strlen(cursor_values[0]), strlen(cursor_values[1]) };
   int cursor_value_formats[2] = { 0, 0 };

   if (!r -> handler || strcmp (r -> handler, "pgasp-handler") ) return DECLINED;
   if (!r -> method || (strcmp (r -> method, "GET") && strcmp (r -> method, "POST")) ) return DECLINED;

   if (config->is_enabled != true) return OK; /* pretending we have responded, may return DECLINED in the future */

   requested_file = apr_pstrdup (r -> pool, r -> path_info /*filename*/);
   i = strlen(requested_file) - 1;

   while (i > 0)
   {
     if (requested_file[i] == '.') filename_length = i;
     if (requested_file[i] == '/') break;
     i--;
   }

   if (i >= 0) {
     requested_file += i+1; /* now pointing to foo.pgasp instead of /var/www/.../foo.pgasp */
     if (filename_length > i) filename_length -= i+1;
   }

   allowed_to_serve = false;

   for (i = 0; i < config->allowed_count; i++)
   {
      if (!strcmp(config->allowed[i], requested_file))
      {
         allowed_to_serve = true;
         break;
      }
   }
   if (config->allowed_count == 0) allowed_to_serve = true;

   if (!allowed_to_serve)
   {
      ap_set_content_type(r, "text/plain");
      ap_rprintf(r, "Hello there\nThis is PGASP\nEnabled: %s\n", config->is_enabled ? "On" : "Off");
      ap_rprintf(r, "Requested: %s\n", requested_file);
      ap_rprintf(r, "Allowed: %s\n", allowed_to_serve ? "Yes" : "No");

      return OK; /* pretending we have served the file, may return HTTP_FORDIDDEN in the future */
   }

   if (filename_length == 0) {
     basename = requested_file;
   } else {
     basename = apr_pstrndup(r->pool, requested_file, filename_length);
   }

   ap_args_to_table(r, &GETargs);
   if (OK != ap_parse_form_data(r, NULL, &POST, -1, (~((apr_size_t)0)))) {
     __(r->server, " ** ap_parse_form_data is NOT OK");
   }
   GET = (NULL == GET) ? GETargs : apr_table_overlay(r->pool, GETargs, GET);

   // move all POST parameters into GET table
   {
     ap_form_pair_t *pair;
     char *buffer;
     apr_off_t len;
     apr_size_t size;
     while (NULL != (pair = apr_array_pop(POST))) {
       apr_brigade_length(pair->value, 1, &len);
       size = (apr_size_t) len;
       buffer = apr_palloc(r->pool, size + 1);
       apr_brigade_flatten(pair->value, buffer, &size);
       buffer[len] = 0;
       apr_table_setn(GET, apr_pstrdup(r->pool, pair->name), buffer); //should name and value be ap_unescape_url() -ed?
       //       __(r->server, "POST[%s]: %s", pair->name, buffer);
     }
   }

   params.r = r;
   params.args = NULL;
   apr_table_do(tab_args, &params, GET, NULL);
   params.args = apr_pstrcat(r->pool, "&", params.args, "&", NULL);

   cursor_values[0] = params.args;
   cursor_value_lengths[0] = strlen(cursor_values[0]);

   /* set response content type according to configuration or to default value */
   ap_set_content_type(r, dir_config->content_type_set ? dir_config->content_type : "text/html");

   /* now connecting to Postgres, getting function output, and printing it */

   pgc = pgasp_pool_open (r->server);

   if (PQstatus(pgc) != CONNECTION_OK)
   {
      spit_pg_error ("connect");
      pgasp_pool_close(r->server, pgc);
      return OK;
   }

   /* removing extention (.pgasp or other) from file name, and adding "f_" for function name, i.e. foo.pgasp becomes psp_foo() */
   snprintf(cursor_string,
	    sizeof(cursor_string),
	    "select * from f_%s($1::varchar)",
	    basename);

   /* passing GET as first (and only) parameter */
   if (0 == PQsendQueryParams (pgc, cursor_string, 1, NULL, cursor_values, cursor_value_lengths, cursor_value_formats, 0)) {
      spit_pg_error ("sending async query with params");
      return clean_up_connection(r->server);
   }

   if (0 == PQsetSingleRowMode(pgc)) {
     ap_log_error(APLOG_MARK, APLOG_WARNING, 0, r->server, "can not fall into single raw mode to fetch data");
   }

   while (NULL != (pgr = PQgetResult(pgc))) {

     if (PQresultStatus(pgr) != PGRES_TUPLES_OK && PQresultStatus(pgr) != PGRES_SINGLE_TUPLE) {
       spit_pg_error ("fetch data");
       return clean_up_connection(r->server);
     }

     /* the following counts and for-loop may seem excessive as it's just 1 row/1 field, but might need it in the future */

     field_count = PQnfields(pgr);
     tuple_count = PQntuples(pgr);

     for (i = 0; i < tuple_count; i++)
       {
	 for (j = 0; j < field_count; j++) ap_rprintf(r, "%s", PQgetvalue(pgr, i, j));
	 ap_rprintf(r, "\n");
       }
     PQclear (pgr);
   }
   pgasp_pool_close(r->server, pgc);

   return OK;
}

/************ pgasp cfg: manage db connection pool ****************/
/* an apr_reslist_constructor for PgSQL connections */

static apr_status_t pgasp_pool_construct(void** db, void* params, apr_pool_t* pool) {
  pgasp_config* pgasp = (pgasp_config*) params ;
  PGconn* sql = PQconnectdb (pgasp->connection_string);
  *db = sql ;

  if ( sql )
    return APR_SUCCESS ;
  else
    return APR_EGENERAL ;
}

static apr_status_t pgasp_pool_destruct(void* sql, void* params, apr_pool_t* pool) {
  PQfinish((PGconn*)sql) ;
  return APR_SUCCESS ;
}

static int setup_db_pool(apr_pool_t* p, apr_pool_t* plog,
	apr_pool_t* ptemp, server_rec* s) {

  void *data = NULL;
  char *key;
  const char *userdata_key = "pgasp_post_config";
  apr_hash_index_t *idx;
  apr_ssize_t len;
  pgasp_config *pgasp;

  // This code is used to prevent double initialization of the module during Apache startup
  apr_pool_userdata_get(&data, userdata_key, s->process->pool);
  if ( data == NULL ) {
    apr_pool_userdata_set((const void *)1, userdata_key, apr_pool_cleanup_null, s->process->pool);
    return OK;
  }

  for (idx = apr_hash_first(p, pgasp_pool_config); idx; idx = apr_hash_next(idx)) {

    apr_hash_this(idx, (void *) &key, &len, (void *) &pgasp);

    if ( apr_reslist_create(&pgasp->dbpool,
			    pgasp->nmin,
			    pgasp->nkeep,
			    pgasp->nmax,
			    pgasp->exptime,
			    pgasp_pool_construct,
			    pgasp_pool_destruct,
			    (void*)pgasp, p) != APR_SUCCESS ) {
      ap_log_error(APLOG_MARK, APLOG_CRIT, 0, s, "mod_pgasp: failed to initialise") ;
      return 500 ;
    }
    apr_pool_cleanup_register(p, pgasp->dbpool,
			      (void*)apr_reslist_destroy,
			      apr_pool_cleanup_null) ;
    apr_hash_set(pgasp_pool_config, key, APR_HASH_KEY_STRING, pgasp);

  }
  return OK ;
}


/* Functions we export for modules to use:
	- open acquires a connection from the pool (opens one if necessary)
	- close releases it back in to the pool
*/
PGconn* pgasp_pool_open(server_rec* s) {
  PGconn* ret = NULL ;
  pgasp_config* pgasp = (pgasp_config*)
	ap_get_module_config(s->module_config, &pgasp_module) ;
  apr_uint32_t acquired_cnt ;

  if (pgasp->dbpool == NULL) {
    pgasp = apr_hash_get(pgasp_pool_config, pgasp->key, APR_HASH_KEY_STRING);
  }
  if ( apr_reslist_acquire(pgasp->dbpool, (void**)&ret) != APR_SUCCESS ) {
    ap_log_error(APLOG_MARK, APLOG_ERR, 0, s, "mod_pgasp: Failed to acquire PgSQL connection from pool!") ;
    return NULL ;
  }
  if (PQstatus(ret) != CONNECTION_OK) {
    PQreset(ret);
    if (PQstatus(ret) != CONNECTION_OK) {
      ap_log_error(APLOG_MARK, APLOG_ERR, 0, s,
	"PgSQL Error: %s", PQerrorMessage(ret) ) ;
      apr_reslist_release(pgasp->dbpool, ret) ;
      return NULL ;
    }
  }
  if (pgasp->nkeep < (acquired_cnt = apr_reslist_acquired_count	( pgasp->dbpool	))) {
    ap_log_error(APLOG_MARK, APLOG_WARNING, 0, s, "mod_pgasp: %d connections in the %s pool acquired (%d,%d,%d)",
		 acquired_cnt, pgasp->key, pgasp->nmin, pgasp->nkeep, pgasp->nmax
		 ) ;
  }
  return ret ;
}

void pgasp_pool_close(server_rec* s, PGconn* sql) {
  pgasp_config* pgasp = (pgasp_config*)
	ap_get_module_config(s->module_config, &pgasp_module) ;
  if (pgasp->dbpool == NULL) {
    pgasp = apr_hash_get(pgasp_pool_config, pgasp->key, APR_HASH_KEY_STRING);
  }
  apr_reslist_release(pgasp->dbpool, sql) ;
}

static apr_status_t init_db_pool(apr_pool_t* p, apr_pool_t* plog, apr_pool_t* ptemp) {
  apr_status_t rc = APR_SUCCESS;

  pgasp_pool_config = apr_hash_make(p);
  return rc;
}



static void* create_pgasp_config(apr_pool_t* p, server_rec* s) {
  pgasp_config* config = (pgasp_config*) apr_pcalloc(p, sizeof(pgasp_config)) ;
  config->is_enabled = true;
  config->connection_string = NULL;
  config->allowed_count = 0;
  config->nmax = 1;
  config->exptime = 3600000;
  config->pool = p;
  return config ;
}

static void *merge_pgasp_config(apr_pool_t * p, void *basev, void *addv) {
    pgasp_config *new = (pgasp_config *) apr_pcalloc(p, sizeof(pgasp_config));
    pgasp_config *add = (pgasp_config *) addv;
    pgasp_config *base = (pgasp_config *) basev;

    new->key = (add->key_set == 0) ? base->key : add->key;
    new->key_set = add->key_set || base->key_set;
    new->connection_string = (add->connection_string_set == 0) ? base->connection_string : add->connection_string;
    new->connection_string_set = add->connection_string_set || base->connection_string_set;
    new->ServerName = (add->ServerName_set == 0) ? base->ServerName : add->ServerName;
    new->ServerName_set = add->ServerName_set || base->ServerName_set;
    /* NB: pgasp_config->pool and pgasp_config->dbpool must not be merged */
    new->pool = add->pool;
    new->dbpool = add->dbpool;
    new->nmin = (add->nmin_set == 0) ? base->nmin : add->nmin;
    new->nmin_set = add->nmin_set || base->nmin_set;
    new->nkeep = (add->nkeep_set == 0) ? base->nkeep : add->nkeep;
    new->nkeep_set = add->nkeep_set || base->nkeep_set;
    new->nmax = (add->nmax_set == 0) ? base->nmax : add->nmax;
    new->nmax_set = add->nmax_set || base->nmax_set;
    new->exptime = (add->exptime_set == 0) ? base->exptime : add->exptime;
    new->exptime_set = add->exptime_set || base->exptime_set;
    new->is_enabled = (add->is_enabled_set == 0) ? base->is_enabled : add->is_enabled;
    new->is_enabled_set = add->is_enabled_set || base->is_enabled_set;

    return new;
}



static void* create_dir_config(apr_pool_t* p, char* x) {
  pgasp_dir_config *conf = apr_pcalloc(p, sizeof(pgasp_dir_config)) ;

  conf->dir = x;
  conf->content_type = NULL;
  conf->content_type_set = 0;

  return conf ;
}

static void *merge_dir_config(apr_pool_t * p, void *basev, void *addv) {
    pgasp_dir_config *new = (pgasp_dir_config *) apr_pcalloc(p, sizeof(pgasp_dir_config));
    pgasp_dir_config *add = (pgasp_dir_config *) addv;
    pgasp_dir_config *base = (pgasp_dir_config *) basev;

    new->content_type = (add->content_type_set == 0) ? base->content_type : add->content_type;
    new->content_type_set = add->content_type_set || base->content_type_set;

    return new;
}



static void register_hooks (apr_pool_t * pool)
{
    static const char * const aszPre[]={ "http_core.c", "http_vhost.c", NULL };
    ap_hook_pre_config (init_db_pool, NULL, NULL, APR_HOOK_MIDDLE) ;
    ap_hook_post_config (setup_db_pool, aszPre, NULL, APR_HOOK_LAST) ;
    ap_hook_handler (pgasp_handler, NULL, NULL, APR_HOOK_LAST);
}

module AP_MODULE_DECLARE_DATA pgasp_module =
{
    STANDARD20_MODULE_STUFF,
    create_dir_config,
    merge_dir_config,
    create_pgasp_config,
    merge_pgasp_config,
    pgasp_directives,
    register_hooks
};

