APXS=apxs
APXS_CFLAGS=
APXS_LFLAGS=
APXS_LIBS=
CC=gcc
CFLAGS=-O2
PG_CFLAGS="-I$(shell pg_config --includedir)"
PG_LFLAGS="-L$(shell pg_config --libdir) -lpq"

all: pgaspc mod_pgasp.la

install: all
	@sudo $(APXS) -i -a -n pgasp mod_pgasp.la

pgaspc: pgaspc.c
	@$(CC) -o $@ $(CFLAGS) $<

mod_pgasp.la: mod_pgasp.c
	@$(APXS) -c -o $@ $(APXS_CFLAGS) $(PG_CFLAGS) $(APXS_LFLAGS) $(PG_LFLAGS) $(APXS_LIBS) $< --shared

clean:
	@rm -rf *~ *.la *.lo *.slo .libs

dist-clean: clean
	@rm -f pgaspc
