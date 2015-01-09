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
 * 2015-01-09 Added comments section in the beginning of .pgasp
 *
 * TODO: PHP wrapper generation
 * TODO: different variables declaration section (for parsing GET/POST) when generated for use with mod_pgasp
 * TODO: Error handling
 *
 */

//#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>

#define true 1
#define false 0

#define MAX_INPUT_CHARS 4090

int       in_code = false, in_equals = false, in_comment = false, tag_processed = false, is_first_line = true;
FILE *    f;
char      line_input [MAX_INPUT_CHARS + 4];
char *    line_trimmed;
int       i;

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
         if (line_input[i] == 0x0d || line_input[i] == 0x0a)
         {
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
         printf("create or replace function f_%s (", line_trimmed);
         is_first_line = false;
      }
      else
      {
         /* variables declaration tag <! !> */
         if (line_trimmed[0] == '<' && line_trimmed[1] == '!')
         {
            printf(")\nreturns text as $$\ndeclare\n_pgasp_ text;");
            line_trimmed += 2;
         }

         if (line_trimmed[0] == '!' && line_trimmed[1] == '>')
         {
            printf("begin\n_pgasp_ := \'");
            line_trimmed += 2;
         }

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

            printf("%c", line_trimmed[i]);
            i++;

         } /* regular line */

      } /* not first line */

      printf("\n");

   } /* while fgets */

   printf("\';\nreturn _pgasp_;\nend;\n$$\nlanguage plpgsql;\n\n\\q\n");

   fclose (f);
   exit (EXIT_SUCCESS);

} /* main */

