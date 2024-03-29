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


## Inserting into the relation

The following callbacks are available for inserting into a tuple:

| Function                     | Description                                         |
|:-----------------------------|:----------------------------------------------------|
| `tuple_insert`               | Normal insertion function                           |
| `tuple_insert_speculative`   | Insertion function for speculative inserts          |
| `tuple_complete_speculative` | Completion function to complete speculative inserts |

### Speculative insertion

The two last functions in the table above are for speculative
insert. Speculative inserts can be used when there is `INSERT ... ON
CONFLICT` clause with the insert as well as indices.

The speculative insert will attempt an insert, but if there is a
conflict, it will be backed out of without aborting the transaction.

Each speculative insert will generate a speculative insert token
`specToken` that can be used to identify the particular insertion
being done.

With the completion function, there is a boolean indicating if the
insertion was successful, or if it generated conflicts. If it
generated conflicts, the *insert* has to be aborted, but not the
transaction.

### Potential improvement for performance

These functions pass in a relation as part of the call, but there is
no way to pass state information back and forth between these
functions and PostgreSQL.

If there is an inner state in the table access methods you need to use
static variables to maintain the state. This is comparably fragile
since it requires calls to come in a particular order: if they don't,
things might break.

It also prevents PostgreSQL from running several insert operations
concurrently.

There is also a potential for a race condition if you have an internal
state consisting of tables that needs to be opened and closed, since
it is not clear when the table can be closed the final time.

For example, in this sequence diagram, the outer table is open (and
locked) for the duration of the sequence, but the inner table is open
and closed multiple times, which *could* lead to a race condition
between the inserts if not handled correctly.

```sequence{theme="hand"}
Title: Call sequence for inserting into table
PostgreSQL->Table: table_open(outer)
PostgreSQL->nodeModifyTable: ExecInsert(outer)
nodeModifyTable->TableAM: tuple_insert(outer)
TableAM->HeapAM: table_open(inner)
TableAM->HeapAM: tuple_insert(inner)
TableAM->HeapAM: table_close(inner)
nodeModifyTable-->TableAM
nodeModifyTable->TableAM: tuple_insert(outer)
TableAM->HeapAM: table_open(inner)
TableAM->HeapAM: tuple_insert(inner)
TableAM->HeapAM: table_close(inner)
nodeModifyTable-->TableAM
nodeModifyTable->TableAM: tuple_insert(outer)
TableAM->HeapAM: table_open(inner)
TableAM->HeapAM: tuple_insert(inner)
TableAM->HeapAM: table_close(inner)
nodeModifyTable-->TableAM
TableAM-->PostgreSQL
PostgreSQL->Table: table_close(outer)
```

The outer table is opened once and for all, but since there is no
state to store the opened inner table, it has to be re-opened each
time.

It might be possible to store the state in a static variable inside
the table access method, but then there is no callback to indicate
when the table can be closed.

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

## Modifying a relation

Updates, deletes, and merges are built on top of a scan. For each tuple
satisfying the predicate, the latest tuple version is fetched via
`table_tuple_fetch_row_version`, then changed via `table_tuple_update`
et al.  Concurrent modifications may result in a retry, depending on the
isolation level, in which case the row may be `table_tuple_lock`ed.

These all have a similar problem to inserts, in that the inner relation
must be continually reopened to access the inner tuples.

## Truncating a relation

A relation is typically truncated by setting a different file node for
the relation using `table_relation_set_new_filenode`, but there is a
`table_relation_nontransactional_truncate` available as well.

The non-transactional version is used when the filenode is created in
the same transaction as the truncate, for example when a new table is
created in the same transaction as the truncate or if the relation is
truncated twice in the same transaction.

The transactional version will register the old filenode for unlinking
using the `RelationDropStorage` function, and then create and assign a
new filenode and assign it to the relation. This means that it is not
possible to unlink the old relation and an internal table is required
to store the mapping between the old filenode and the new filenode to
be able to unlink the old filenode.

```mermaid
sequenceDiagram
	tablecmds->>relcache: RelationSetNewRelfilenode(relation)
	relcache->>catalog: newrelfilenode := GetNewRelFileNode()
	relcache->>storage: RelationDropStorage(relation)
	note over relcache: rel->rd_node := newrelfilenode
	relcache->>tableam: table_relation_set_new_filenode(relation, relation->rd_node)
```

> A better approach would be to provide the extension with information
> that a file node is unlinked. From the perspective of the extension,
> this is just an object identifier that it can use any way it likes,
> so either it can be passed as a parameter, or an additional callback
> can be added that will reset the filenode. 
> 
> Adding a reset filenode callback would bnot require any signatures
> nor changes to existing table access methods, and would also support
> table drops as well as truncates.

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

