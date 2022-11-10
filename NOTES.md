# Notes on PostgreSQL and table access methods

## Directory structure

A reasonable directory structure for implementing a table access
method is this:

```
.
├── expected
│   ├──    .
│   ├──    .
│   ├──    .
│   └── basic.out
├── LICENSE
├── Makefile
├── README.md
├── sql
│   └── basic.sql
├── src
│   ├── traceam_handler.c
│   ├── traceam.c
│   ├── traceam.h
│   ├── trace.h
│   ├──    .
│   ├──    .
│   └──    .
├── traceam--0.1.sql
└── traceam.control
```

| File                    | Description                                                        |
|:------------------------|:-------------------------------------------------------------------|
| `Makefile`              | Makefile for the package                                           |
| `traceam.control`       | Control file for extension                                         |
| `traceam--X.Y.sql`      | Install files for extension                                        |
| `src/traceam_handler.c` | Contains the implementation of the table access method handler     |
| `src/traceam.*`         | Contains the implementation of the guts of the table access method |
| `sql/*.sql`             | Test files for extension                                           |
| `expected/*.out`        | Reference files for tests                                          |


## Scanning a relation

The typical execution of a scan is roughly this:

```c
Relation rel = table_open(...);
TableScanDesc scan = table_beginscan(rel, ...);
while (table_scan_getnextslot(scan, ...)) {
   ...
}
table_endscan(scan);
table_close(rel);
```

Note that the table is opened over the entire sequence of calls so
that we internally need to open the guts table in a similar
manner. However, we do not have access to the locks that are being
taken for the "main" table, so we have to speculate on what lock to
take. (The lock needed is available in the range table entry, but not
available in the relation structure, AFAICT.)

## Getting information

### Relation owner

To get a relation owner for an open relation it is not necessary to
read `pg_class`, it is available in the `rd_rel` field as `relowner`.

```c
Relation relation;
   .
   .
   .
Oid owner = relation->rd_rel->relowner
```

