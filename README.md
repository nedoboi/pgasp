pgasp
=====

Adaptive Server Pages for Postgres

* mod_pgasp.c - Apache module, connects directly to Postgres bypassing PHP/Perl/Python
* pgaspc.c - PGASP Compiler, creates Postgres function from .pgasp file
* demo/ - directory with function, data and configuration files to create a demo site
  * pgasp.conf - a virtual host confuguration file for Apache to setup the demo site
  * index.html - a simple index file cantaining the links to all samples listed below
  * create_sample_PGASP_CRUD.sql - Creates sample schema used in CRUD and other examples
  * birthday_paradox.pgasp - Example without data access, just simple calculations
  * browse_people.pgasp - Example with data access, lists database table
  * return_json_example.* - Static .html file gets JSON from .pgasp and renders as table
  * manage_security.pgasp - CRUD (create, retrieve, update, delete) example
  * return_google_charts.pgasp - Google Charts example, shows pie, bar, and org charts

How this works
==============

The following packages are needed to build and install it on Ubuntu: make apache2 apache2-dev

## Apache module installation
To install mod_pgasp module into your Apache httpd-server, just type the command:

```
make install
```
Though you would need to have rights to execute commands under sudo to complete this.

## Demo site installation
To create a demo web-site on Debian/Ubuntu, just type the command:

```
make demo
```
it will build and install mod_pgasp module and install and enable configuration for pgasp.local
web-site, so you need only to add the following line into your /etc/hosts file to make it working:

```
127.0.0.1 pgasp.local
```

By default it uses local connection via socket, username 'postgres' without password and
database name 'pgasp' to configure PgSQL connection for demosite.
If you need to change any of these, provide new values via standard
PgSQL environment variables before executing 'make demo'. E.g.:

```
PGHOST=faraway.galaxy PGUSER=darth PGPASSWORD=force PGDATABASE=deathstar make demo
```

You need to have the rigts to execute commands under sudo to perform all these procedures.

## General (old) instructions

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


```
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

