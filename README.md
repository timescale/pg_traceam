# Trace access methods

Simple table access method that just prints out what functions in the
access methods and related functions that are called.

## Installing

To install the engine, make sure you have `pg_config` in the path, and
then run:

```bash
make
make install
```

Once the extension is installed it is necessary to install it in the
database as well, which is done using `CREATE EXTENSION`:

```sql
CREATE EXTENSION traceam;
```

## How to use

The error messages are printed at debug level 2, so to enable tracing,
you need to set `client_min_messages` to `debug2`. You can then create
a new table using the table access method and work with it. For
example:

```sql
mats=# SET client_min_messages TO debug2;
SET
mats=# CREATE TABLE foo (a int) USING traceam;
DEBUG:  traceam_relation_set_new_filenode relation: foo
DEBUG:  traceam_relation_needs_toast_table relation: foo
DEBUG:  EventTriggerInvoke 17467
CREATE TABLE
```
