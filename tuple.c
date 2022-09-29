#include "tuple.h"

#include <postgres.h>

#include <executor/tuptable.h>

typedef struct TraceTupleTableSlot {
  TupleTableSlot base;
} TraceTupleTableSlot;

static void tts_trace_init(TupleTableSlot *slot) {
  elog(DEBUG2, "slot: %p", slot);
}

static void tts_trace_release(TupleTableSlot *slot) {
  elog(DEBUG2, "slot: %p %s", slot, nodeToString((Node *)slot));
}

static void tts_trace_clear(TupleTableSlot *slot) {
  elog(DEBUG2, "slot: %p %s", slot, nodeToString((Node *)slot));
  if (unlikely(TTS_SHOULDFREE(slot)))
    slot->tts_flags &= ~TTS_FLAG_SHOULDFREE;

  slot->tts_nvalid = 0;
  slot->tts_flags |= TTS_FLAG_EMPTY;
  ItemPointerSetInvalid(&slot->tts_tid);
}

static void tts_trace_materialize(TupleTableSlot *slot) {
  elog(DEBUG2, "slot: %p %s", slot, nodeToString((Node *)slot));
}

static void tts_trace_copyslot(TupleTableSlot *dstslot,
                               TupleTableSlot *srcslot) {
  TupleDesc srcdesc = srcslot->tts_tupleDescriptor;
  elog(DEBUG2, "dstslot: %p, srcslot: %p %s", dstslot, srcslot,
       nodeToString((Node *)srcslot));

  Assert(srcdesc->natts <= dstslot->tts_tupleDescriptor->natts);

  tts_trace_clear(dstslot);

  slot_getallattrs(srcslot);

  for (int natt = 0; natt < srcdesc->natts; natt++) {
    dstslot->tts_values[natt] = srcslot->tts_values[natt];
    dstslot->tts_isnull[natt] = srcslot->tts_isnull[natt];
  }

  dstslot->tts_nvalid = srcdesc->natts;
  dstslot->tts_flags &= ~TTS_FLAG_EMPTY;

  tts_trace_materialize(dstslot);
}

static Datum tts_trace_getsysattr(TupleTableSlot *slot, int attnum,
                                  bool *isnull) {
  elog(DEBUG2, "attnum: %d, slot: %s", attnum, nodeToString((Node *)slot));
  return 0; /* silence compiler warnings */
}

static void tts_trace_getsomeattrs(TupleTableSlot *slot, int natts) {
  elog(DEBUG2, "natts: %d, slot: %s", natts, nodeToString((Node *)slot));
}

static HeapTuple tts_trace_copy_heap_tuple(TupleTableSlot *slot) {
  elog(DEBUG2, "slot: %s", nodeToString((Node *)slot));

  Assert(!TTS_EMPTY(slot));

  return heap_form_tuple(slot->tts_tupleDescriptor, slot->tts_values,
                         slot->tts_isnull);
}

static MinimalTuple tts_trace_copy_minimal_tuple(TupleTableSlot *slot) {
  elog(DEBUG2, "slot: %s", nodeToString((Node *)slot));

  Assert(!TTS_EMPTY(slot));

  return heap_form_minimal_tuple(slot->tts_tupleDescriptor, slot->tts_values,
                                 slot->tts_isnull);
}

const TupleTableSlotOps TTSOpsTraceTuple = {
    .base_slot_size = sizeof(TraceTupleTableSlot),
    .init = tts_trace_init,
    .release = tts_trace_release,
    .clear = tts_trace_clear,
    .getsomeattrs = tts_trace_getsomeattrs,
    .getsysattr = tts_trace_getsysattr,
    .materialize = tts_trace_materialize,
    .copyslot = tts_trace_copyslot,

    .get_heap_tuple = NULL,
    .get_minimal_tuple = NULL,
    .copy_heap_tuple = tts_trace_copy_heap_tuple,
    .copy_minimal_tuple = tts_trace_copy_minimal_tuple,
};
