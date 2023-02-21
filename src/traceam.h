#ifndef TRACEAM_H_
#define TRACEAM_H_

#include <postgres.h>

#include <access/tableam.h>

#define TRACEAM_SCHEMA_NAME "traceam"

typedef struct TraceScanDescData {
  TableScanDescData rs_base;
  TableScanDesc guts_scan;
} TraceScanDescData;

typedef struct TraceScanDescData* TraceScanDesc;

#if PG_MAJORVERSION_NUM < 16
/*
 * 16devel renamed several structs and members to get rid of the overloaded
 * "node" term. These are compatibility shims for 15 and prior.
 */
# define RelFileLocator RelFileNode
# define RelFileNumber  Oid

# define spcOid         spcNode
# define dbOid          dbNode
# define relNumber      relNode

# define relation_set_new_filelocator relation_set_new_filenode
#endif

void trace_create_filenode(Relation relation, const RelFileLocator* newrlocator,
                           char persistance);
Relation trace_open_filenode(Oid relfilenode, LOCKMODE lockmode);
void trace_close(Relation relation, LOCKMODE lockmode);

#endif /* TRACEAM_H_ */
