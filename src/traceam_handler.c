#include <postgres.h>

#include <access/amapi.h>
#include <access/heapam.h>
#include <access/tableam.h>
#include <catalog/heap.h>
#include <catalog/index.h>
#include <catalog/namespace.h>
#include <catalog/pg_am_d.h>
#include <commands/tablespace.h>
#include <commands/vacuum.h>
#include <executor/tuptable.h>
#include <miscadmin.h>
#include <utils/rel.h>
#include <utils/syscache.h>

#include "trace.h"
#include "traceam.h"
#include "tuple.h"

PG_MODULE_MAGIC;

PG_FUNCTION_INFO_V1(traceam_handler);

void _PG_init(void);

static const TableAmRoutine traceam_methods;

/* A static variable with the open relation is needed to handle
   table_tuple_insert_speculative and table_tuple_complete_speculative
   since they do not have a state variable being passed in. This only
   works if the calls are in the right order. */
static Relation open_relation;

static const char *itemPointerToString(ItemPointer pointer) {
  static char buf[32];
  sprintf(buf,
          "%u/%u",
          ItemPointerGetBlockNumber(pointer),
          ItemPointerGetOffsetNumber(pointer));
  return buf;
}

static const TupleTableSlotOps *traceam_slot_callbacks(Relation relation) {
  Relation guts;
  const TupleTableSlotOps *callbacks;

  TRACE("relation: %s", RelationGetRelationName(relation));
  guts = trace_open_filenode(relation->rd_rel->relfilenode, AccessShareLock);
  callbacks = table_slot_callbacks(guts);
  table_close(guts, AccessShareLock);
  return callbacks;
}

/**
 * Start a scan of the trace table.
 *
 * We try to mimic the typical execution of the scanning functions,
 * which is:
 *
 *    rel = table_open(...);
 *    scan = table_beginscan(rel, ...);
 *    while (table_scan_getnextslot(scan, ...)) {
 *      ...
 *    }
 *    table_endscan(scan);
 *    table_close(rel);
 *
 * @note we do not take a lock here since we instead rely on the
 * locking to be synchronized over the the "real" relation.
 */
static TableScanDesc traceam_scan_begin(Relation relation, Snapshot snapshot,
                                        int nkeys, ScanKey key,
                                        ParallelTableScanDesc parallel_scan,
                                        uint32 flags) {
  Relation guts;
  TraceScanDesc scan;

  TRACE("relation: %s, nkeys: %d, flags: %x",
        RelationGetRelationName(relation),
        nkeys,
        flags);
  RelationIncrementReferenceCount(relation);

  scan = (TraceScanDesc)palloc(sizeof(TraceScanDescData));
  scan->rs_base.rs_rd = relation;
  scan->rs_base.rs_snapshot = snapshot;
  scan->rs_base.rs_nkeys = nkeys;
  scan->rs_base.rs_flags = flags;
  scan->rs_base.rs_parallel = parallel_scan;

  guts = trace_open_filenode(relation->rd_rel->relfilenode, AccessShareLock);
  scan->guts_scan = guts->rd_tableam->scan_begin(
      guts, snapshot, nkeys, key, parallel_scan, flags);
  return (TableScanDesc)scan;
}

static void traceam_scan_end(TableScanDesc sscan) {
  TraceScanDesc scan = (TraceScanDesc)sscan;
  TRACE("relation: %s", RelationGetRelationName(sscan->rs_rd));
  RelationDecrementReferenceCount(scan->rs_base.rs_rd);
  table_close(scan->guts_scan->rs_rd, AccessShareLock);
  table_endscan(scan->guts_scan);
}

static void traceam_scan_rescan(TableScanDesc sscan, ScanKey key,
                                bool set_params, bool allow_strat,
                                bool allow_sync, bool allow_pagemode) {
  TraceScanDesc scan = (TraceScanDesc)sscan;
  TRACE("relation: %s", RelationGetRelationName(sscan->rs_rd));
  scan->guts_scan->rs_rd->rd_tableam->scan_rescan(scan->guts_scan,
                                                  key,
                                                  set_params,
                                                  allow_strat,
                                                  allow_sync,
                                                  allow_pagemode);
}

static bool traceam_scan_getnextslot(TableScanDesc sscan,
                                     ScanDirection direction,
                                     TupleTableSlot *slot) {
  TraceScanDesc scan = (TraceScanDesc)sscan;
  TRACE("relation: %s", RelationGetRelationName(sscan->rs_rd));
  TRACE_DETAIL("slot: %s", slotToString(slot));
  /* We are storing the data in the slot for the outer table, not the
   * inner table. We probably need to use the slot for the inner table
   * and then copy the columns to the outer table slot. */
  return table_scan_getnextslot(scan->guts_scan, direction, slot);
}

static IndexFetchTableData *traceam_index_fetch_begin(Relation relation) {
  TRACE("relation: %s", RelationGetRelationName(relation));
  return NULL;
}

static void traceam_index_fetch_reset(IndexFetchTableData *scan) {
  /* nothing to do here */
}

static void traceam_index_fetch_end(IndexFetchTableData *scan) {
  /* nothing to do here */
}

static bool traceam_index_fetch_tuple(struct IndexFetchTableData *scan,
                                      ItemPointer tid, Snapshot snapshot,
                                      TupleTableSlot *slot, bool *call_again,
                                      bool *all_dead) {
  TRACE("tid: %s", itemPointerToString(tid));
  TRACE_DETAIL("slot: %s", slotToString(slot));
  return 0;
}

static bool traceam_fetch_row_version(Relation relation, ItemPointer tid,
                                      Snapshot snapshot, TupleTableSlot *slot) {
  Relation inner;
  bool result;
  TRACE("relation: %s", RelationGetRelationName(relation));
  TRACE_DETAIL("slot: %s", slotToString(slot));
  inner = trace_open_filenode(relation->rd_rel->relfilenode, AccessShareLock);
  /* XXX see notes above regarding copying slots */
  result = table_tuple_fetch_row_version(inner, tid, snapshot, slot);
  table_close(inner, NoLock);
  return result;
}

static void traceam_get_latest_tid(TableScanDesc scan, ItemPointer tid) {
  TRACE("relation: %s", RelationGetRelationName(scan->rs_rd));
}

static bool traceam_tuple_tid_valid(TableScanDesc scan, ItemPointer tid) {
  TRACE("relation: %s", RelationGetRelationName(scan->rs_rd));
  return false;
}

static bool traceam_tuple_satisfies_snapshot(Relation relation,
                                             TupleTableSlot *slot,
                                             Snapshot snapshot) {
  TRACE("relation: %s", RelationGetRelationName(relation));
  TRACE_DETAIL("slot: %s", slotToString(slot));
  return false;
}

static TransactionId traceam_index_delete_tuples(Relation relation,
                                                 TM_IndexDeleteOp *delstate) {
  TRACE("relation: %s", RelationGetRelationName(relation));
  return InvalidTransactionId;
}

static void traceam_tuple_insert(Relation relation, TupleTableSlot *slot,
                                 CommandId cid, int options,
                                 BulkInsertState bistate) {
  Relation guts;
  TRACE("relation: %s, cid: %d", RelationGetRelationName(relation), cid);
  TRACE_DETAIL("slot: %s", slotToString(slot));
  guts = trace_open_filenode(relation->rd_rel->relfilenode, RowExclusiveLock);
  table_tuple_insert(guts, slot, cid, options, bistate);
  table_close(guts, NoLock);
}

static void traceam_tuple_insert_speculative(Relation relation,
                                             TupleTableSlot *slot,
                                             CommandId cid, int options,
                                             BulkInsertState bistate,
                                             uint32 specToken) {
  TRACE("relation: %s, cid: %d", RelationGetRelationName(relation), cid);
  TRACE_DETAIL("slot: %s", slotToString(slot));
  open_relation =
      trace_open_filenode(relation->rd_rel->relfilenode, RowExclusiveLock);
  table_tuple_insert_speculative(
      open_relation, slot, cid, options, bistate, specToken);
}

static void traceam_tuple_complete_speculative(Relation relation,
                                               TupleTableSlot *slot,
                                               uint32 spekToken,
                                               bool succeeded) {
  TRACE("relation: %s", RelationGetRelationName(relation));
  TRACE_DETAIL("slot: %s", slotToString(slot));
  table_close(open_relation, NoLock);
}

static void traceam_multi_insert(Relation relation, TupleTableSlot **slots,
                                 int ntuples, CommandId cid, int options,
                                 BulkInsertState bistate) {
  Relation inner;
  TRACE("relation: %s, cid: %u, ntuples: %d",
        RelationGetRelationName(relation),
        cid,
        ntuples);
  inner = trace_open_filenode(relation->rd_rel->relfilenode, RowExclusiveLock);
  table_multi_insert(inner, slots, ntuples, cid, options, bistate);
  table_close(inner, NoLock);
}

static TM_Result traceam_tuple_delete(Relation relation, ItemPointer tid,
                                      CommandId cid, Snapshot snapshot,
                                      Snapshot crosscheck, bool wait,
                                      TM_FailureData *tmfd, bool changingPart) {
  Relation inner;
  TM_Result result;
  TRACE("relation: %s, cid: %d", RelationGetRelationName(relation), cid);
  inner = trace_open_filenode(relation->rd_rel->relfilenode, RowExclusiveLock);
  result = table_tuple_delete(inner, tid, cid, snapshot, crosscheck, wait, tmfd,
                              changingPart);
  table_close(inner, NoLock);
  return result;
}

static TM_Result traceam_tuple_update(Relation relation, ItemPointer otid,
                                      TupleTableSlot *slot, CommandId cid,
                                      Snapshot snapshot, Snapshot crosscheck,
                                      bool wait, TM_FailureData *tmfd,
                                      LockTupleMode *lockmode,
                                      bool *update_indexes) {
  Relation inner;
  TM_Result result;
  TRACE("relation: %s, cid: %d", RelationGetRelationName(relation), cid);
  TRACE_DETAIL("slot: %s", slotToString(slot));
  inner = trace_open_filenode(relation->rd_rel->relfilenode, RowExclusiveLock);
  result = table_tuple_update(inner, otid, slot, cid, snapshot, crosscheck,
                              wait, tmfd, lockmode, update_indexes);
  table_close(inner, NoLock);
  return result;
}

static TM_Result traceam_tuple_lock(Relation relation, ItemPointer tid,
                                    Snapshot snapshot, TupleTableSlot *slot,
                                    CommandId cid, LockTupleMode mode,
                                    LockWaitPolicy wait_policy, uint8 flags,
                                    TM_FailureData *tmfd) {
  Relation inner;
  TM_Result result;
  TRACE("relation: %s, cid: %d", RelationGetRelationName(relation), cid);
  TRACE_DETAIL("slot: %s", slotToString(slot));
  inner = trace_open_filenode(relation->rd_rel->relfilenode, AccessShareLock);
  result = table_tuple_lock(inner, tid, snapshot, slot, cid, mode, wait_policy,
                            flags, tmfd);
  table_close(inner, NoLock);
  return result;
}

static void traceam_finish_bulk_insert(Relation relation, int options) {
  TRACE("relation: %s", RelationGetRelationName(relation));
  /* nothing to do */
}

static void traceam_relation_set_new_filelocator(Relation relation,
                                                 const RelFileLocator *newrlocator,
                                                 char persistence,
                                                 TransactionId *freezeXid,
                                                 MultiXactId *minmulti) {
  TRACE("relation: %s, newrnode: {spcNode: %u, dbNode: %u, relNode: %u}",
        RelationGetRelationName(relation),
        newrlocator->spcOid,
        newrlocator->dbOid,
        newrlocator->relNumber);
  /* On a truncate, the old relfilenode is scheduled for unlinking
     before this function is called, so we never see the old file
     node.  We need to have a table to map the existing relation to
     the file node to be able to modify this internally. */
  trace_create_filenode(relation, newrlocator, persistence);
}

static void traceam_relation_nontransactional_truncate(Relation relation) {
  Relation guts;
  TRACE("relation: %s", RelationGetRelationName(relation));
  guts =
      trace_open_filenode(relation->rd_rel->relfilenode, AccessExclusiveLock);
  table_relation_nontransactional_truncate(guts);
  table_close(guts, AccessExclusiveLock);
}

static void traceam_copy_data(Relation relation, const RelFileLocator *newrlocator) {
  TRACE("relation: %s", RelationGetRelationName(relation));
#if 0
  /* This needs to copy to a new filenode, so we need to create a
     table for the guts before copying. */
  Relation guts;
  guts = guts_open(RelationGetRelid(relation), RowExclusiveLock);
  table_relation_copy_data(guts, newgnode);
  guts_close(guts, RowExclusiveLock);
#endif
}

static void traceam_copy_for_cluster(Relation old_table, Relation new_table,
                                     Relation OldIndex, bool use_sort,
                                     TransactionId OldestXmin,
                                     TransactionId *xid_cutoff,
                                     MultiXactId *multi_cutoff,
                                     double *num_tuples, double *tups_vacuumed,
                                     double *tups_recently_dead) {
  TRACE("old_table: %s, new_table: %s",
        RelationGetRelationName(old_table),
        RelationGetRelationName(new_table));
}

static void traceam_vacuum(Relation relation, VacuumParams *params,
                           BufferAccessStrategy bstrategy) {
  TRACE("relation: %s", RelationGetRelationName(relation));
}

static bool traceam_scan_analyze_next_block(TableScanDesc scan,
                                            BlockNumber blockno,
                                            BufferAccessStrategy bstrategy) {
  TRACE("relation: %s", RelationGetRelationName(scan->rs_rd));
  return false;
}

static bool traceam_scan_analyze_next_tuple(TableScanDesc scan,
                                            TransactionId OldestXmin,
                                            double *liverows, double *deadrows,
                                            TupleTableSlot *slot) {
  TRACE("relation: %s", RelationGetRelationName(scan->rs_rd));
  return false;
}

static double traceam_index_build_range_scan(
    Relation tableRelation, Relation indexRelation, IndexInfo *indexInfo,
    bool allow_sync, bool anyvisible, bool progress, BlockNumber start_blockno,
    BlockNumber numblocks, IndexBuildCallback callback, void *callback_state,
    TableScanDesc scan) {
  TRACE("%s scan, table: %s, index: %s, relation: %s",
        scan ? "parallel" : "serial",
        RelationGetRelationName(tableRelation),
        RelationGetRelationName(indexRelation),
        scan ? RelationGetRelationName(scan->rs_rd) : "<>");
  return 0;
}

static void traceam_index_validate_scan(Relation tableRelation,
                                        Relation indexRelation,
                                        IndexInfo *indexInfo, Snapshot snapshot,
                                        ValidateIndexState *state) {
  TRACE("table: %s, index: %s",
        RelationGetRelationName(tableRelation),
        RelationGetRelationName(indexRelation));
}

static uint64 traceam_relation_size(Relation relation, ForkNumber forkNumber) {
  TRACE("relation: %s", RelationGetRelationName(relation));
  return 0;
}

static bool traceam_relation_needs_toast_table(Relation relation) {
  TRACE("relation: %s", RelationGetRelationName(relation));
  return false;
}

static void traceam_estimate_rel_size(Relation relation, int32 *attr_widths,
                                      BlockNumber *pages, double *tuples,
                                      double *allvisfrac) {
  TRACE("relation: %s", RelationGetRelationName(relation));
  *attr_widths = 0;
  *tuples = 0;
  *allvisfrac = 0;
  *pages = 0;
}

static bool traceam_scan_bitmap_next_block(TableScanDesc scan,
                                           TBMIterateResult *tbmres) {
  TRACE("relation: %s", RelationGetRelationName(scan->rs_rd));
  return false;
}

static bool traceam_scan_bitmap_next_tuple(TableScanDesc scan,
                                           TBMIterateResult *tbmres,
                                           TupleTableSlot *slot) {
  TRACE("relation: %s", RelationGetRelationName(scan->rs_rd));
  return false;
}

static bool traceam_scan_sample_next_block(TableScanDesc scan,
                                           SampleScanState *scanstate) {
  TRACE("relation: %s", RelationGetRelationName(scan->rs_rd));
  return false;
}

static bool traceam_scan_sample_next_tuple(TableScanDesc scan,
                                           SampleScanState *scanstate,
                                           TupleTableSlot *slot) {
  TRACE("relation: %s", RelationGetRelationName(scan->rs_rd));
  return false;
}

static const TableAmRoutine traceam_methods = {
    .type = T_TableAmRoutine,

    .slot_callbacks = traceam_slot_callbacks,

    .scan_begin = traceam_scan_begin,
    .scan_end = traceam_scan_end,
    .scan_rescan = traceam_scan_rescan,
    .scan_getnextslot = traceam_scan_getnextslot,

    .parallelscan_estimate = table_block_parallelscan_estimate,
    .parallelscan_initialize = table_block_parallelscan_initialize,
    .parallelscan_reinitialize = table_block_parallelscan_reinitialize,

    .index_fetch_begin = traceam_index_fetch_begin,
    .index_fetch_reset = traceam_index_fetch_reset,
    .index_fetch_end = traceam_index_fetch_end,
    .index_fetch_tuple = traceam_index_fetch_tuple,

    .tuple_insert = traceam_tuple_insert,
    .tuple_insert_speculative = traceam_tuple_insert_speculative,
    .tuple_complete_speculative = traceam_tuple_complete_speculative,
    .multi_insert = traceam_multi_insert,
    .tuple_delete = traceam_tuple_delete,
    .tuple_update = traceam_tuple_update,
    .tuple_lock = traceam_tuple_lock,
    .finish_bulk_insert = traceam_finish_bulk_insert,

    .tuple_fetch_row_version = traceam_fetch_row_version,
    .tuple_get_latest_tid = traceam_get_latest_tid,
    .tuple_tid_valid = traceam_tuple_tid_valid,
    .tuple_satisfies_snapshot = traceam_tuple_satisfies_snapshot,
    .index_delete_tuples = traceam_index_delete_tuples,

    .relation_set_new_filelocator = traceam_relation_set_new_filelocator,
    .relation_nontransactional_truncate =
        traceam_relation_nontransactional_truncate,
    .relation_copy_data = traceam_copy_data,
    .relation_copy_for_cluster = traceam_copy_for_cluster,
    .relation_vacuum = traceam_vacuum,
    .scan_analyze_next_block = traceam_scan_analyze_next_block,
    .scan_analyze_next_tuple = traceam_scan_analyze_next_tuple,
    .index_build_range_scan = traceam_index_build_range_scan,
    .index_validate_scan = traceam_index_validate_scan,

    .relation_size = traceam_relation_size,
    .relation_needs_toast_table = traceam_relation_needs_toast_table,

    .relation_estimate_size = traceam_estimate_rel_size,

    .scan_bitmap_next_block = traceam_scan_bitmap_next_block,
    .scan_bitmap_next_tuple = traceam_scan_bitmap_next_tuple,
    .scan_sample_next_block = traceam_scan_sample_next_block,
    .scan_sample_next_tuple = traceam_scan_sample_next_tuple,
};

Datum traceam_handler(PG_FUNCTION_ARGS) {
  PG_RETURN_POINTER(&traceam_methods);
}

void _PG_init(void) {
}
