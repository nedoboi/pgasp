pgasp
=====

Adaptive Server Pages for Postgres

1. mod_pgasp.c - Apache module, connects directly to Postgres bypassing PHP/Perl/Python
2. pgaspc.c - PGASP Compiler, creates Postgres function from .pgasp file
3. create_sample_PGASP_CRUD.sql - Creates sample schema used in CRUD and other examples
4. birthday_paradox.pgasp - Example without data access, just simple calculations
5. browse_people.pgasp - Example with data access, lists database table
6. return_json_example.* - Static .html file gets JSON from .pgasp and renders as table
7. manage_security.pgasp - CRUD (create, retrieve, update, delete) example
8. return_google_charts.pgasp - Google Charts example, shows pie, bar, and org charts

How this works
==============

1. Download and compile PGASP compiler (with gcc)
2. Download and compile Apache module (with apxs), enable module in Apache config
3. Download and run .sql file to create sample schema
4. Download and compile a .pgasp file (with pgaspc)
5. Allow compiled .pgasp file in Apache config, restart Apache service
6. Go to http://your-apache-server-host/file_name_example.pgasp
7. Enjoy

Notes
=====

See more detailed instructions inside the files

Do NOT put source .pgasp files in /var/www/html, Apache does NOT need them, they are compiled directly into Postgres

Go to http://pgasp.org for more information

.pgasp file format
==================


```html
#
# comments here
#
# some more comments here
#

file_name (without .pgasp)
parameter type default_value (for example, filter_name varchar John*)
parameter type default_value (for example, p_id integer 123)
<!

local_variable type;
local_variable type default default_value;

!>
<%

PL/pgSQL code here

%>
<html> or [ (for JSON) or <xml> or <svg> or whatever
<%

Some more PL/pgSQL code here

%>
<= PL/pgSQL variable here =>
<%

Yet more PL/pgSQL code here

%>
</html> or ] or </xml> or </svg> or whatever
```

