APXS=apxs
APXS_CFLAGS=
APXS_LFLAGS=
APXS_LIBS=
CC=gcc
CFLAGS=-O2
PG_CFLAGS="-I$(shell pg_config --includedir)"
PG_LFLAGS="-L$(shell pg_config --libdir) -lpq"
PSQL=psql
PGASPC=./pgaspc
PGDATABASE?=pgasp
PGHOST?=/var/run/postgresql
PGUSER?=postgres

all: pgaspc mod_pgasp.la

install: all
	@sudo $(APXS) -i -a -n pgasp mod_pgasp.la

pgaspc: pgaspc.c
	@$(CC) -o $@ $(CFLAGS) $<

mod_pgasp.la: mod_pgasp.c
	@$(APXS) -c -o $@ $(APXS_CFLAGS) $(PG_CFLAGS) $(APXS_LFLAGS) $(PG_LFLAGS) $(APXS_LIBS) $< --shared

demo: install
	@-createdb $(PGDATABASE) ; find ./demo -name "*.pgasp" -exec sh -c "$(PGASPC) {} | $(PSQL) $(PGDATABASE)" \;
	@$(PSQL) --file=demo/create_sample_PGASP_CRUD.sql $(PGDATABASE)
	@sudo mkdir -p /var/www/pgasp
	@sudo cp demo/*.html /var/www/pgasp
	@sudo cp demo/pgasp.conf `$(APXS) -q sysconfdir`/sites-available/
	@sudo sed -i "s!dbname=[^ ]*!dbname=$(PGDATABASE)!;s!host=[^ ]*!host=$(PGHOST)!;s!user=[^\" ]*!user=$(PGUSER)!;s!password=[^\" ]*!password=$(PGPASSWORD)!" `$(APXS) -q sysconfdir`/sites-available/pgasp.conf
	@sudo service apache2 restart

clean:
	@rm -rf *~ *.la *.lo *.slo .libs

dist-clean: clean
	@rm -f pgaspc
