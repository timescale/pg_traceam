#ifndef TRACEAM_H_
#define TRACEAM_H_

#include <postgres.h>

#include <access/tableam.h>

typedef struct TraceScanDescData {
  TableScanDescData rs_base;
  TableScanDesc guts_scan;
} TraceScanDescData;

typedef struct TraceScanDescData* TraceScanDesc;

extern const TupleTableSlotOps* traceam_slot_callbacks(Relation relation);

#endif /* TRACEAM_H_ */
