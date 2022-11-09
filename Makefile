MODULE_big = traceam
OBJS = traceam.o tuple.o guts.o

EXTENSION = traceam
DATA = traceam--0.1.sql
PGFILEDESC = "traceam - table access method that prints trace of calls"
PG_CFLAGS = -std=c99

REGRESS = basic
REGRESS_OPTS += --load-extension=traceam

PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)

traceam.o: traceam.c tuple.h
tuple.o: tuple.c tuple.h
