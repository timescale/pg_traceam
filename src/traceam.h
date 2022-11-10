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

void trace_create_filenode(Relation relation, const RelFileNode* newrnode,
                           char persistance);
Relation trace_open_filenode(Oid relfilenode, LOCKMODE lockmode);
void trace_close(Relation relation, LOCKMODE lockmode);

#endif /* TRACEAM_H_ */
