/*
 * PGASP (Adaptive Server Pages for Postgres) Compiler
 * Author: "Alex Nedoboi" <my seven-letter surname at gmail>
 *
 * Input: .pgasp file format, see pgasp.org for specifications and examples
 *
 * Output: 1. Creates PL/pgSQL function source to standard output, ready to pipe to psql
 *         2. Creates .php wrapper file (optional) - TODO
 *
 * Compilation: gcc -o pgaspc pgaspc.c
 *
 * Usage: pgaspc input_file_name.pgasp | psql -h host -d database -U user
 *
 * 2014-12-30 Started
 * 2015-01-03 Added classic style print tag <%= %> along with new style <= =>
 * 2015-01-04 Added checks for consecutive tags
 * 2015-01-09 .pgasp format now allows a comments section in the beginning of the file
 * 2015-01-14 Added in_declare, in_header
 * 2015-01-14 Printing extra single quote if not in code/equals/declare
 * 2015-01-17 Now getting _pgasp_get_ from Apache mod_pgasp
 * 2015-01-18 Added in_params
 *
 * TODO: PHP wrapper generation
 * TODO: different variables declaration section (for parsing GET/POST) when generated for use with mod_pgasp
 * TODO: Error handling
 *
 * TOTHINK: Return a record instead of just text, i.e. (mime, body) or maybe (headers, body)
 *          ("text/html", "<!DOCTYPE html><html> ... </html>")
 *          ("application/json", "{ ... }")
 *          ("image/svg+xml", "<svg version='1.1'> ... </svg>")
 * TOTHINK: Do we really need in_declare in_header in_params? Perhaps can be simplified?
 *
 */

//#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>

#define true 1
#define false 0

#define MAX_INPUT_CHARS 4090

int       in_code = false, in_equals = false, in_comment = false, in_declare = false, in_header = true, in_params = false;
int       tag_processed = false, is_first_line = true;
FILE *    f;
char      line_input [MAX_INPUT_CHARS + 4];
char *    line_trimmed;
int       i, j;

int main(int argc, char * argv[])
{
   fprintf(stderr, "\nPGASP Compiler beta\n\n");

   f = fopen (argv[1], "r");
   if (f == NULL) { exit (EXIT_FAILURE); }

   while (fgets (line_input, MAX_INPUT_CHARS, f) != NULL)
   {
      i = 0;
      while (line_input[i])
      {
         /* removing new-line characters */
         if (line_input[i] == 0x0d || line_input[i] == 0x0a)
         {
            /* adding extra end-of-string zeroes to avoid out-of-bound comparisons */
            line_input[i] = line_input[i+1] = line_input[i+2] = line_input[i+3] = 0x00;
         }

         i++;
      }

      line_trimmed = line_input;
      while (line_trimmed[0] == ' ' || line_trimmed[0] == '\t') { line_trimmed++; }

      /* comments section in the beginning of .pgasp file, only works until first non-comment line */
      if (line_trimmed[0] == '#' && is_first_line) line_trimmed[0] = 0;

      if (line_trimmed[0] == 0) continue;

      if (is_first_line)
      {
         printf("create or replace function f_%s (_pgasp_GET_ varchar", line_trimmed);
         printf(")\nreturns text as $$\ndeclare\n_pgasp_ text;");

         is_first_line = false;
         in_params = true;
      }
      else
      {
         /* variables declaration tag <! !> */
         if (line_trimmed[0] == '<' && line_trimmed[1] == '!')
         {
            in_header = false; /* as soon as we reach the declare section, the header section stops */
            in_params = false;
            in_declare = true;
            line_trimmed += 2;
         }

         if (line_trimmed[0] == '!' && line_trimmed[1] == '>')
         {
            in_declare = false;
            printf("begin\n_pgasp_ := \'");
            line_trimmed += 2;
         }

         /* processing parameters in HTTP GET passed by mod_apache */
         if (in_params)
         {
            i = 0;

            /* finding first and second white spaces */
            while (line_trimmed[i] != ' ' && line_trimmed[i] != '\t') i++;
            j = i;
            while (line_trimmed[i] == ' ' || line_trimmed[i] == '\t') i++;
            while (line_trimmed[i] != ' ' && line_trimmed[i] != '\t') i++;
            while (line_trimmed[i] == ' ' || line_trimmed[i] == '\t') i++;

            /* Input  : parameter type default
               Parsed : parameter [j] type [i] default
               Output : parameter type := pgasp_parse_get(_pgasp_GET_, 'parameter', 'default'); */

            printf("%.*s:= pgasp_parse_get(_pgasp_GET_, \'%.*s\', \'%s\');\n", i, line_trimmed, j, line_trimmed, line_trimmed+i);

            continue;
         }

         /* function name, passed parameters, and declared variables - done, now processing the rest */
         i = 0;
         while (line_trimmed[i])
         {
            do
            {
               tag_processed = false; /* to cover 2+ consecutive tags */

               /* code tag <% %> but not print tag <%= %> */
               if (line_trimmed[i] == '<' && line_trimmed[i+1] == '%' && line_trimmed[i+2] != '=')
               {
                  in_code = true;
                  printf("\';");

                  i += 2;
                  tag_processed = true;
               }

               if (line_trimmed[i] == '%' && line_trimmed[i+1] == '>')
               {
                  if (in_code)
                  {
                     in_code = false;
                     printf("_pgasp_ := _pgasp_ || \'");
                  }

                  if (in_equals)
                  {
                     in_equals = false;
                     printf(") || \'");
                  }

                  i += 2;
                  tag_processed = true;
               }

               /* new style print tag <= => */
               if (line_trimmed[i] == '<' && line_trimmed[i+1] == '=')
               {
                  in_equals = true;
                  printf("\' || (");

                  i += 2;
                  tag_processed = true;
               }

               /* classic style print tag <%= %> */
               if (line_trimmed[i] == '<' && line_trimmed[i+1] == '%' && line_trimmed[i+2] == '=')
               {
                  in_equals = true;
                  printf("\' || (");

                  i += 3;
                  tag_processed = true;
               }

               if (line_trimmed[i] == '=' && line_trimmed[i+1] == '>')
               {
                  in_equals = false;
                  printf(") || \'");

                  i += 2;
                  tag_processed = true;
               }

            }
            while (tag_processed);

            if (!in_code && !in_equals && !in_declare && !in_header && line_trimmed[i] == '\'') printf("%c", line_trimmed[i]);

            if (line_trimmed[i]) printf("%c", line_trimmed[i]);

            i++;

         } /* regular line */

      } /* not first line */

      printf("\n");

   } /* while fgets */

   printf("\';\nreturn _pgasp_;\nend;\n$$\nlanguage plpgsql;\n\n\\q\n");

   fclose (f);
   exit (EXIT_SUCCESS);

} /* main */

