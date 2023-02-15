MODULE_big = traceam
OBJS = src/traceam.o src/traceam_handler.o src/tuple.o

EXTENSION = traceam
DATA = traceam--0.1.sql
PGFILEDESC = "traceam - table access method that prints trace of calls"
PG_CFLAGS = -std=c99
PG_CPPFLAGS = -Isrc

REGRESS = basic
REGRESS_OPTS += --load-extension=traceam

ISOLATION = iso_basic
ISOLATION_OPTS += --load-extension=traceam

PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)

traceam.o: src/traceam.c src/traceam.h src/trace.h
traceam_handler.o: src/traceam_handler.c src/trace.h src/traceam.h \
 src/tuple.h
tuple.o: src/tuple.c src/tuple.h
