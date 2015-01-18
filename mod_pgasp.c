/*
 * mod_pgasp.c - Apache module for PGASP (Adaptive Server Pages for Postgres)
 * Author: "Alex Nedoboi" <my seven-letter surname at gmail>
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

#include <stdio.h>
#include <stdlib.h>
#include <libpq-fe.h>

#include "httpd.h"
#include "http_core.h"
#include "http_config.h"
#include "http_log.h"
#include "http_protocol.h"
#include "http_request.h"
#include "apr_strings.h"

#define spit_pg_error(st) { ap_rprintf(r,"<!-- "); ap_rprintf(r,"Cannot %s: %s\n",st,PQerrorMessage(pgc)); ap_rprintf(r," -->\n"); }
#define MAX_ALLOWED_PAGES 100

#define true 1
#define false 0

static int allowed_count = 0;
PGconn * pgc;
PGresult * pgr;

typedef struct
{
   int is_enabled;
   const char * connection_string;
   const char * allowed[MAX_ALLOWED_PAGES];
}
pgasp_config;

static pgasp_config config;

const char * pgasp_set_enabled (cmd_parms * cmd, void * cfg, const char * arg)
{
    if (!strcasecmp(arg, "on")) config.is_enabled = true; else config.is_enabled = false;
    return NULL;
}

const char * pgasp_set_connect (cmd_parms * cmd, void * cfg, const char * arg)
{
   config.connection_string = arg;
   return NULL;
}

const char * pgasp_set_allowed (cmd_parms * cmd, void * cfg, const char * arg)
{
   if (allowed_count < MAX_ALLOWED_PAGES) config.allowed[allowed_count++] = arg;
   return NULL;
}

static const command_rec pgasp_directives[] =
{
   AP_INIT_TAKE1("pgaspEnabled",          pgasp_set_enabled, NULL, RSRC_CONF, "Enable or disable mod_pgasp"),
   AP_INIT_TAKE1("pgaspConnectionString", pgasp_set_connect, NULL, RSRC_CONF, "Postgres connection string"),
   AP_INIT_TAKE1("pgaspAllowed",          pgasp_set_allowed, NULL, RSRC_CONF, "Web pages allowed to be served"),
   { NULL }
};

static int clean_up_connection ()
{
    PQclear (pgr);
    PQfinish (pgc);
    return OK;
}

static int pgasp_handler (request_rec * r)
{
   //apr_table_t * GET;
   //apr_array_header_t * POST;
   int i, j, allowed_to_serve;
   int field_count, tuple_count;
   char * requested_file;
   char cursor_string[256];

   /* PQexecParams doesn't seem to like zero-length strings, so we feed it a dummy */
   const char * dummy_get = "nothing";

   const char * cursor_values[1] = { r -> args ? r -> args : dummy_get };
   int cursor_value_lengths[1] = { strlen(r -> args ? r -> args : dummy_get) };
   int cursor_value_formats[1] = { 0 };

   if (!r -> handler || strcmp (r -> handler, "pgasp-handler") ) return DECLINED;
   if (!config.is_enabled) return OK; /* pretending we have responded, may return DECLINED in the future */

   requested_file = apr_pstrdup (r -> pool, r -> filename);
   i = strlen(requested_file) - 1;

   while (i > 0)
   {
      if (requested_file[i] == '/') break;
      i--;
   }

   if (i >= 0) requested_file += i+1; /* now pointing to foo.pgasp instead of /var/www/.../foo.pgasp */

   allowed_to_serve = false;

   for (i = 0; i < allowed_count; i++)
   {
      if (!strcmp(config.allowed[i], requested_file))
      {
         allowed_to_serve = true;
         break;
      }
   }

   if (!allowed_to_serve)
   {
      ap_set_content_type(r, "text/plain");
      ap_rprintf(r, "Hello there\nThis is PGASP\nEnabled: %s\n", config.is_enabled ? "On" : "Off");
      ap_rprintf(r, "Requested: %s\n", requested_file);
      ap_rprintf(r, "Allowed: %s\n", allowed_to_serve ? "Yes" : "No");

      return OK; /* pretending we have served the file, may return HTTP_FORDIDDEN in the future */
   }

   /* need to think of how to return mime type from Postgres function, perhaps return a record instead of just text */
   ap_set_content_type(r, "text/html");
//   ap_set_content_type(r, "application/json");

   /* now connecting to Postgres, getting function output, and printing it */

   pgc = PQconnectdb (config.connection_string);

   if (PQstatus(pgc) != CONNECTION_OK)
   {
      spit_pg_error ("connect");
      PQfinish(pgc);
      return OK;
   }

   pgr = PQexec (pgc, "begin");
   if (PQresultStatus(pgr) != PGRES_COMMAND_OK)
   {
      spit_pg_error ("start transaction");
      return clean_up_connection();
   }
   PQclear (pgr);

   /* removing ".pgasp" from file name, and adding "f_" for function name, i.e. foo.pgasp becomes f_foo() */
   sprintf(cursor_string, "declare c cursor for select f_%.*s($1::varchar) as f", (int) strlen(requested_file)-6, requested_file);

   /* passing GET as first (and only) parameter */
   pgr = PQexecParams (pgc, cursor_string, 1, NULL, cursor_values, cursor_value_lengths, cursor_value_formats, 0);
   if (PQresultStatus(pgr) != PGRES_COMMAND_OK)
   {
      spit_pg_error ("declare cursor");
      return clean_up_connection();
   }
   PQclear (pgr);

   pgr = PQexec (pgc, "fetch all in c");
   if (PQresultStatus(pgr) != PGRES_TUPLES_OK)
   {
      spit_pg_error ("fetch data");
      return clean_up_connection();
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

   pgr = PQexec (pgc, "close c");
   PQclear (pgr);

   pgr = PQexec (pgc, "end");
   PQclear (pgr);
   PQfinish (pgc);

   return OK;
}

static void register_hooks (apr_pool_t * pool)
{
    config.is_enabled = true;
    config.connection_string = "host=127.0.0.1";
    ap_hook_handler (pgasp_handler, NULL, NULL, APR_HOOK_LAST);
}

module AP_MODULE_DECLARE_DATA pgasp_module =
{
    STANDARD20_MODULE_STUFF,
    NULL, NULL, NULL, NULL, /* No specific handling */
    pgasp_directives,
    register_hooks
};

