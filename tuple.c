#include "tuple.h"

#include <postgres.h>

#include <inttypes.h>

#include <executor/tuptable.h>
#include <lib/stringinfo.h>
#include <nodes/memnodes.h>

/**
 * Trace macro.
 *
 * This is used by the callbacks to emit a trace prefixed with the
 * function that is being called.
 */
#define TRACE(FMT, ...) ereport(DEBUG2, \
                                (errmsg_internal("%s " FMT, __func__, ##__VA_ARGS__), \
                                 errbacktrace()));

/*
 * nodeToString() doesn't handle TupleTableSlots at the moment, so do it
 * manually here.
 */
char *
slotToString(TupleTableSlot *slot)
{
  StringInfoData str;
  int    i;

  initStringInfo(&str);

  appendStringInfoString(&str, "{TUPLETABLESLOT");
  appendStringInfo(&str, " :tts_flags 0x%X", slot->tts_flags);
  appendStringInfo(&str, " :tts_nvalid %d", slot->tts_nvalid);
  appendStringInfo(&str, " :tts_ops %p", slot->tts_ops);
  appendStringInfo(&str, " :tts_tupleDescriptor %p", slot->tts_tupleDescriptor);

  appendStringInfoString(&str, " :tts_values ");
  if (!slot->tts_values)
    appendStringInfoString(&str, "<>");
  else
  {
    appendStringInfoChar(&str, '(');
    for (i = 0; i < slot->tts_nvalid; i++)
    {
      if (i)
        appendStringInfoChar(&str, ' ');
      appendStringInfo(&str, "%" PRIdPTR, slot->tts_values[i]);
    }
    appendStringInfoChar(&str, ')');
  }

  appendStringInfoString(&str, " :tts_isnull ");
  if (!slot->tts_isnull)
    appendStringInfoString(&str, "<>");
  else
  {
    appendStringInfoChar(&str, '(');
    for (i = 0; i < slot->tts_nvalid; i++)
    {
      if (i)
        appendStringInfoChar(&str, ' ');
      appendStringInfoString(&str, slot->tts_isnull[i] ? "true" : "false");
    }
    appendStringInfoChar(&str, ')');
  }

  appendStringInfoString(&str, " :tts_mcxt {MEMORYCONTEXT :name ");
  outToken(&str, slot->tts_mcxt->name);
  appendStringInfoString(&str, " :ident ");
  outToken(&str, slot->tts_mcxt->ident);
  appendStringInfoChar(&str, '}');

  appendStringInfo(&str, " :tts_tid %u:%u",
                   BlockIdGetBlockNumber(&slot->tts_tid.ip_blkid),
                   slot->tts_tid.ip_posid);
  appendStringInfo(&str, " :tts_tableOid %u", slot->tts_tableOid);

  appendStringInfoChar(&str, '}');
  return str.data;
}

static void tts_trace_init(TupleTableSlot *slot) {
  TraceTupleTableSlot *tslot = (TraceTupleTableSlot *) slot;
  TRACE("slot: %p", slot);

  tslot->wrapped = MakeSingleTupleTableSlot(slot->tts_tupleDescriptor,
                                            &TTSOpsBufferHeapTuple);
  /* TODO: sync */
}

static void tts_trace_release(TupleTableSlot *slot) {
  TraceTupleTableSlot *tslot = (TraceTupleTableSlot *) slot;
  TRACE("slot: %p %s", slot, slotToString(slot));

  ExecDropSingleTupleTableSlot(tslot->wrapped);
}

/*
 * Copies the wrapped slot's base attributes into the top-level trace slot.
 * Typically this should be done whenever the wrapped slot is passed to a
 * function that modifies it.
 */
void SyncTraceTupleTableSlot(TraceTupleTableSlot *tslot)
{
  tslot->base.tts_flags = tslot->wrapped->tts_flags;
  tslot->base.tts_nvalid = tslot->wrapped->tts_nvalid;

  /* skip tts_ops */

  if (tslot->base.tts_tupleDescriptor != tslot->wrapped->tts_tupleDescriptor)
  {
    if (tslot->base.tts_tupleDescriptor)
      ReleaseTupleDesc(tslot->base.tts_tupleDescriptor);

    tslot->base.tts_tupleDescriptor = tslot->wrapped->tts_tupleDescriptor;

    if (tslot->base.tts_tupleDescriptor)
      PinTupleDesc(tslot->base.tts_tupleDescriptor);
  }

  /* XXX: this will lead to a double-free in the non-FIXED case */
  if (unlikely(tslot->base.tts_tupleDescriptor == NULL))
    elog(ERROR, "FIXME: SyncTraceTupleTableSlot does not support non-FIXED slots");
  tslot->base.tts_values = tslot->wrapped->tts_values;
  tslot->base.tts_isnull = tslot->wrapped->tts_isnull;

  /* skip tts_mcxt */

  tslot->base.tts_tid = tslot->wrapped->tts_tid;
  tslot->base.tts_tableOid = tslot->wrapped->tts_tableOid;
}

static void tts_trace_clear(TupleTableSlot *slot) {
  TraceTupleTableSlot *tslot = (TraceTupleTableSlot *) slot;
  TRACE("slot: %p %s", slot, slotToString(slot));

  ExecClearTuple(tslot->wrapped);
  SyncTraceTupleTableSlot(tslot);
}

static void tts_trace_materialize(TupleTableSlot *slot) {
  TraceTupleTableSlot *tslot = (TraceTupleTableSlot *) slot;
  TRACE("slot: %p %s", slot, slotToString(slot));

  ExecMaterializeSlot(tslot->wrapped);
  SyncTraceTupleTableSlot(tslot);
}

static void tts_trace_copyslot(TupleTableSlot *dstslot,
                               TupleTableSlot *srcslot) {
  TraceTupleTableSlot *tslot = (TraceTupleTableSlot *) dstslot;
  TRACE("dstslot: %p %s, srcslot: %p %s", dstslot, slotToString(dstslot),
        srcslot, slotToString(srcslot));

  ExecCopySlot(tslot->wrapped, srcslot);
  SyncTraceTupleTableSlot(tslot);
}

static Datum tts_trace_getsysattr(TupleTableSlot *slot, int attnum,
                                  bool *isnull) {
  TRACE("attnum: %d, slot: %s", attnum, slotToString(slot));
  return 0; /* silence compiler warnings */
}

static void tts_trace_getsomeattrs(TupleTableSlot *slot, int natts) {
  TraceTupleTableSlot *tslot = (TraceTupleTableSlot *) slot;
  TRACE("natts: %d, slot: %s", natts, slotToString(slot));

  slot_getsomeattrs_int(tslot->wrapped, natts);
  SyncTraceTupleTableSlot(tslot);
}

static HeapTuple tts_trace_copy_heap_tuple(TupleTableSlot *slot) {
  TRACE("slot: %s", slotToString(slot));

  Assert(!TTS_EMPTY(slot));

  return heap_form_tuple(slot->tts_tupleDescriptor, slot->tts_values,
                         slot->tts_isnull);
}

static MinimalTuple tts_trace_copy_minimal_tuple(TupleTableSlot *slot) {
  TRACE("slot: %s", slotToString(slot));

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
