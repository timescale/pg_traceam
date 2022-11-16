#include <postgres.h>

#include <access/amapi.h>
#include <access/heapam.h>
#include <access/tableam.h>
#include <catalog/index.h>
#include <commands/tablespace.h>
#include <commands/vacuum.h>
#include <executor/tuptable.h>
#include <miscadmin.h>
#include <utils/rel.h>

#include "tuple.h"

PG_MODULE_MAGIC;

PG_FUNCTION_INFO_V1(traceam_handler);

void _PG_init(void);

/**
 * Trace macro.
 *
 * This is used by the callbacks to emit a trace prefixed with the
 * function that is being called.
 */
#define TRACE(FMT, ...) ereport(DEBUG2, \
                                (errmsg_internal("%s " FMT, __func__, ##__VA_ARGS__), \
                                 errbacktrace()));

static const TableAmRoutine traceam_methods;
static const TableAmRoutine *heapam_methods;

static const TupleTableSlotOps *traceam_slot_callbacks(Relation relation) {
  TRACE("relation: %s", RelationGetRelationName(relation));
  return &TTSOpsTraceTuple;
}

static TableScanDesc traceam_scan_begin(Relation relation, Snapshot snapshot,
                                        int nkeys, ScanKey key,
                                        ParallelTableScanDesc parallel_scan,
                                        uint32 flags) {
  TRACE("relation: %s, nkeys: %d, flags: %x", RelationGetRelationName(relation),
        nkeys, flags);
  return heapam_methods->scan_begin(relation, snapshot, nkeys, key,
                                    parallel_scan, flags);
}

static void traceam_scan_end(TableScanDesc scan) {
  TRACE("relation: %s", RelationGetRelationName(scan->rs_rd));
  return heapam_methods->scan_end(scan);
}

static void traceam_scan_rescan(TableScanDesc scan, ScanKey key,
                                bool set_params, bool allow_strat,
                                bool allow_sync, bool allow_pagemode) {
  TRACE("relation: %s", RelationGetRelationName(scan->rs_rd));
}

static bool traceam_scan_getnextslot(TableScanDesc scan,
                                     ScanDirection direction,
                                     TupleTableSlot *slot) {
  TraceTupleTableSlot *tslot = (TraceTupleTableSlot *) slot;
  bool valid;

  TRACE("relation: %s, slot: %s", RelationGetRelationName(scan->rs_rd),
        slotToString(slot));

  tslot->wrapped->tts_tableOid = slot->tts_tableOid;
  TraceEnsureNoSlotChanges(tslot, DIR_INCOMING);

  valid = heapam_methods->scan_getnextslot(scan, direction, tslot->wrapped);

  slot->tts_flags = tslot->wrapped->tts_flags;
  slot->tts_nvalid = tslot->wrapped->tts_nvalid;
  slot->tts_tid = tslot->wrapped->tts_tid;
  TraceEnsureNoSlotChanges(tslot, DIR_OUTGOING);

  return valid;
}

static IndexFetchTableData *traceam_index_fetch_begin(Relation relation) {
  TRACE("relation: %s", RelationGetRelationName(relation));
  return heapam_methods->index_fetch_begin(relation);
}

static void traceam_index_fetch_reset(IndexFetchTableData *scan) {
  TRACE("relation: %s", RelationGetRelationName(scan->rel));
  return heapam_methods->index_fetch_reset(scan);
}

static void traceam_index_fetch_end(IndexFetchTableData *scan) {
  TRACE("relation: %s", RelationGetRelationName(scan->rel));
  return heapam_methods->index_fetch_end(scan);
}

static bool traceam_index_fetch_tuple(struct IndexFetchTableData *scan,
                                      ItemPointer tid, Snapshot snapshot,
                                      TupleTableSlot *slot, bool *call_again,
                                      bool *all_dead) {
  TraceTupleTableSlot *tslot = (TraceTupleTableSlot *) slot;
  bool valid;

  TRACE("relation: %s, tid: %u:%u, slot: %s",
        RelationGetRelationName(scan->rel),
        BlockIdGetBlockNumber(&tid->ip_blkid), tid->ip_posid,
        slotToString(slot));

  TraceEnsureNoSlotChanges(tslot, DIR_INCOMING);

  valid = heapam_methods->index_fetch_tuple(scan, tid, snapshot, tslot->wrapped,
                                            call_again, all_dead);

  slot->tts_flags = tslot->wrapped->tts_flags;
  slot->tts_tid = tslot->wrapped->tts_tid;
  slot->tts_tableOid = tslot->wrapped->tts_tableOid;
  TraceEnsureNoSlotChanges(tslot, DIR_OUTGOING);

  return valid;
}

static bool traceam_fetch_row_version(Relation relation, ItemPointer tid,
                                      Snapshot snapshot, TupleTableSlot *slot) {
  TraceTupleTableSlot *tslot = (TraceTupleTableSlot *) slot;
  bool valid;

  TRACE("relation: %s, slot: %p %s", RelationGetRelationName(relation),
        slot, slotToString(slot));

  TraceEnsureNoSlotChanges(tslot, DIR_INCOMING);

  valid = heapam_methods->tuple_fetch_row_version(relation, tid, snapshot,
                                                  tslot->wrapped);

  slot->tts_flags = tslot->wrapped->tts_flags;
  slot->tts_tid = tslot->wrapped->tts_tid;
  slot->tts_tableOid = tslot->wrapped->tts_tableOid;
  TraceEnsureNoSlotChanges(tslot, DIR_OUTGOING);

  return valid;
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
  TRACE("relation: %s, slot: %s", RelationGetRelationName(relation),
        slotToString(slot));
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
  TraceTupleTableSlot *tslot = (TraceTupleTableSlot *) slot;
  TRACE("relation: %s, slot: %s", RelationGetRelationName(relation),
        slotToString(slot));

  tslot->wrapped->tts_tableOid = slot->tts_tableOid;
  TraceEnsureNoSlotChanges(tslot, DIR_INCOMING);

  heapam_methods->tuple_insert(relation, tslot->wrapped, cid, options, bistate);

  slot->tts_tid = tslot->wrapped->tts_tid;
  TraceEnsureNoSlotChanges(tslot, DIR_OUTGOING);
}

static void traceam_tuple_insert_speculative(Relation relation,
                                             TupleTableSlot *slot,
                                             CommandId cid, int options,
                                             BulkInsertState bistate,
                                             uint32 specToken) {
  TRACE("relation: %s, slot: %s", RelationGetRelationName(relation),
        slotToString(slot));
  /* nothing to do */
}

static void traceam_tuple_complete_speculative(Relation relation,
                                               TupleTableSlot *slot,
                                               uint32 spekToken,
                                               bool succeeded) {
  TRACE("relation: %s, slot: %s", RelationGetRelationName(relation),
        slotToString(slot));
  /* nothing to do */
}

static void traceam_multi_insert(Relation relation, TupleTableSlot **slots,
                                 int ntuples, CommandId cid, int options,
                                 BulkInsertState bistate) {
  int i;
  TraceTupleTableSlot *tslot;
  TupleTableSlot **wrapped;

  TRACE("relation: %s, number: %d, options: %X",
        RelationGetRelationName(relation), ntuples, options);
  Assert(ntuples > 0);

  /* Every slot in the array needs to be replaced by its wrapped buffer slot. */
  wrapped = palloc(ntuples * sizeof(TupleTableSlot *));

  for (i = 0; i < ntuples; ++i) {
      tslot = (TraceTupleTableSlot *) slots[i];

      TraceEnsureNoSlotChanges(tslot, DIR_INCOMING);
      wrapped[i] = tslot->wrapped;
  }

  heapam_methods->multi_insert(relation, wrapped, ntuples, cid, options,
                               bistate);

  /* Translate any changes back to our slot type. */
  for (i = 0; i < ntuples; ++i) {
      tslot = (TraceTupleTableSlot *) slots[i];

      slots[i]->tts_tid = tslot->wrapped->tts_tid;
      TraceEnsureNoSlotChanges(tslot, DIR_OUTGOING);
  }

  /*
   * Note that we're explicitly allowed to leak context memory (in this case,
   * the wrapped array) in this API; maybe for raw speed? Caller will clean up.
   */
}

static TM_Result traceam_tuple_delete(Relation relation, ItemPointer tid,
                                      CommandId cid, Snapshot snapshot,
                                      Snapshot crosscheck, bool wait,
                                      TM_FailureData *tmfd, bool changingPart) {
  TRACE("relation: %s", RelationGetRelationName(relation));
  return TM_Ok;
}

static TM_Result traceam_tuple_update(Relation relation, ItemPointer otid,
                                      TupleTableSlot *slot, CommandId cid,
                                      Snapshot snapshot, Snapshot crosscheck,
                                      bool wait, TM_FailureData *tmfd,
                                      LockTupleMode *lockmode,
                                      bool *update_indexes) {
  TM_Result res;
  TraceTupleTableSlot *tslot = (TraceTupleTableSlot *) slot;

  TRACE("relation: %s, slot: %s", RelationGetRelationName(relation),
        slotToString(slot));

  TraceEnsureNoSlotChanges(tslot, DIR_INCOMING);

  res = heapam_methods->tuple_update(relation, otid, tslot->wrapped, cid,
                                     snapshot, crosscheck, wait, tmfd, lockmode,
                                     update_indexes);

  slot->tts_tid = tslot->wrapped->tts_tid;
  TraceEnsureNoSlotChanges(tslot, DIR_OUTGOING);

  return res;
}

static TM_Result traceam_tuple_lock(Relation relation, ItemPointer tid,
                                    Snapshot snapshot, TupleTableSlot *slot,
                                    CommandId cid, LockTupleMode mode,
                                    LockWaitPolicy wait_policy, uint8 flags,
                                    TM_FailureData *tmfd) {
  TRACE("relation: %s, slot: %s", RelationGetRelationName(relation),
        slotToString(slot));
  return TM_Ok;
}

static void traceam_finish_bulk_insert(Relation relation, int options) {
  TRACE("relation: %s", RelationGetRelationName(relation));
  if (heapam_methods->finish_bulk_insert)
      return heapam_methods->finish_bulk_insert(relation, options);
}

static void traceam_relation_set_new_filenode(Relation relation,
                                              const RelFileNode *newrnode,
                                              char persistence,
                                              TransactionId *freezeXid,
                                              MultiXactId *minmulti) {
  TRACE("relation: %s, newrnode: {spcNode: %d, dbNode: %d, relNode: %d}",
        RelationGetRelationName(relation), newrnode->spcNode, newrnode->dbNode,
        newrnode->relNode);

  return heapam_methods->relation_set_new_filenode(relation, newrnode,
                                                   persistence, freezeXid,
                                                   minmulti);
}

static void traceam_relation_nontransactional_truncate(Relation relation) {
}

static void traceam_copy_data(Relation relation, const RelFileNode *newrnode) {
}

static void traceam_copy_for_cluster(Relation old_table, Relation new_table,
                                     Relation OldIndex, bool use_sort,
                                     TransactionId OldestXmin,
                                     TransactionId *xid_cutoff,
                                     MultiXactId *multi_cutoff,
                                     double *num_tuples, double *tups_vacuumed,
                                     double *tups_recently_dead) {
  TRACE("old_table: %s, new_table: %s", RelationGetRelationName(old_table),
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
  TRACE("table: %s, index: %s", RelationGetRelationName(tableRelation),
        RelationGetRelationName(indexRelation));
}

static uint64 traceam_relation_size(Relation relation, ForkNumber forkNumber) {
  TRACE("relation: %s, fork: %d",
        RelationGetRelationName(relation), forkNumber);
  return heapam_methods->relation_size(relation, forkNumber);
}

static bool traceam_relation_needs_toast_table(Relation relation) {
  TRACE("relation: %s", RelationGetRelationName(relation));
  return heapam_methods->relation_needs_toast_table(relation);
}

static Oid traceam_relation_toast_am(Relation relation) {
  TRACE("relation: %s", RelationGetRelationName(relation));
  return heapam_methods->relation_toast_am(relation);
}

static void traceam_relation_fetch_toast_slice(Relation toastrel, Oid valueid,
                                               int32 attrsize,
                                               int32 sliceoffset,
                                               int32 slicelength,
                                               struct varlena *result) {
  TRACE("relation: %s", RelationGetRelationName(toastrel));
  return heapam_methods->relation_fetch_toast_slice(toastrel, valueid, attrsize,
                                                    sliceoffset, slicelength,
                                                    result);
}

static void traceam_estimate_rel_size(Relation relation, int32 *attr_widths,
                                      BlockNumber *pages, double *tuples,
                                      double *allvisfrac) {
  TRACE("relation: %s", RelationGetRelationName(relation));

  return heapam_methods->relation_estimate_size(relation, attr_widths, pages,
                                                tuples, allvisfrac);
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

    .relation_set_new_filenode = traceam_relation_set_new_filenode,
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
    .relation_toast_am = traceam_relation_toast_am,
    .relation_fetch_toast_slice = traceam_relation_fetch_toast_slice,

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
  heapam_methods = GetHeapamTableAmRoutine();
}
