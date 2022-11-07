MODULE_big = traceam
OBJS = traceam.o tuple.o

EXTENSION = traceam
DATA = traceam--0.1.sql
PGFILEDESC = "traceam - table access method that just print trace of calls"
PG_CFLAGS = -std=gnu99

REGRESS = basic

PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)

traceam.o: traceam.c tuple.h
tuple.o: tuple.c tuple.h
