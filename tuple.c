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

  /*
   * Connect the wrapped slot's values/isnull arrays to ours. This won't work
   * for non-FIXED slots (where we'd create a double-free situation during
   * release), but it's unclear if we need to handle that case for this slot
   * type.
   */
  if (unlikely(slot->tts_tupleDescriptor == NULL))
    elog(ERROR, "FIXME: support non-FIXED slots in tts_trace_init");
  tslot->base.tts_values = tslot->wrapped->tts_values;
  tslot->base.tts_isnull = tslot->wrapped->tts_isnull;
}

static void tts_trace_release(TupleTableSlot *slot) {
  TraceTupleTableSlot *tslot = (TraceTupleTableSlot *) slot;
  TRACE("slot: %p %s", slot, slotToString(slot));

  ExecDropSingleTupleTableSlot(tslot->wrapped);
}

static pg_attribute_noreturn()
void unsupportedSlotModification(const char *attr, int direction)
{
  ereport(ERROR,
          (errmsg("base and wrapped slot differ: %s %s changed %s",
				  (direction == DIR_INCOMING) ? "base" : "wrapped",
		  		  attr,
				  (direction == DIR_INCOMING) ? "externally" : "internally"),
           errbacktrace()));
}

void TraceEnsureNoSlotChanges(TraceTupleTableSlot *tslot, int direction) {
  TupleTableSlot *base = &tslot->base;
  TupleTableSlot *wrap = tslot->wrapped;

  if (base->tts_flags != wrap->tts_flags)
    unsupportedSlotModification("tts_flags", direction);
  if (base->tts_nvalid != wrap->tts_nvalid)
    unsupportedSlotModification("tts_nvalid", direction);
  /* skip tts_ops; they won't be the same */
  if (base->tts_tupleDescriptor != wrap->tts_tupleDescriptor)
    unsupportedSlotModification("tts_tupleDescriptor", direction);
  if (base->tts_values != wrap->tts_values)
    unsupportedSlotModification("tts_values", direction);
  if (base->tts_isnull != wrap->tts_isnull)
    unsupportedSlotModification("tts_isnull", direction);
  /* skip tts_mcxt; we don't care if they're different */
  if (ItemPointerGetBlockNumberNoCheck(&base->tts_tid) != ItemPointerGetBlockNumberNoCheck(&wrap->tts_tid)
      || ItemPointerGetOffsetNumberNoCheck(&base->tts_tid) != ItemPointerGetOffsetNumberNoCheck(&wrap->tts_tid))
    unsupportedSlotModification("tts_tid", direction);
  if (base->tts_tableOid != wrap->tts_tableOid)
    unsupportedSlotModification("tts_tableOid", direction);
}

static void tts_trace_clear(TupleTableSlot *slot) {
  TraceTupleTableSlot *tslot = (TraceTupleTableSlot *) slot;
  TRACE("slot: %p %s", slot, slotToString(slot));

  TraceEnsureNoSlotChanges(tslot, DIR_INCOMING);

  ExecClearTuple(tslot->wrapped);

  slot->tts_flags = tslot->wrapped->tts_flags;
  slot->tts_nvalid = tslot->wrapped->tts_nvalid;
  slot->tts_tid = tslot->wrapped->tts_tid;
  TraceEnsureNoSlotChanges(tslot, DIR_OUTGOING);
}

static void tts_trace_materialize(TupleTableSlot *slot) {
  TraceTupleTableSlot *tslot = (TraceTupleTableSlot *) slot;
  TRACE("slot: %p %s", slot, slotToString(slot));

  tslot->wrapped->tts_flags = slot->tts_flags;
  tslot->wrapped->tts_nvalid = slot->tts_nvalid;
  tslot->wrapped->tts_tableOid = slot->tts_tableOid;
  TraceEnsureNoSlotChanges(tslot, DIR_INCOMING);

  ExecMaterializeSlot(tslot->wrapped);

  slot->tts_flags = tslot->wrapped->tts_flags;
  slot->tts_nvalid = tslot->wrapped->tts_nvalid;
  TraceEnsureNoSlotChanges(tslot, DIR_OUTGOING);
}

static void tts_trace_copyslot(TupleTableSlot *dstslot,
                               TupleTableSlot *srcslot) {
  TraceTupleTableSlot *tslot = (TraceTupleTableSlot *) dstslot;
  TRACE("dstslot: %p %s, srcslot: %p %s", dstslot, slotToString(dstslot),
        srcslot, slotToString(srcslot));

  TraceEnsureNoSlotChanges(tslot, DIR_INCOMING);

  ExecCopySlot(tslot->wrapped, srcslot);

  dstslot->tts_flags = tslot->wrapped->tts_flags;
  dstslot->tts_tid = tslot->wrapped->tts_tid;
  TraceEnsureNoSlotChanges(tslot, DIR_OUTGOING);
}

static Datum tts_trace_getsysattr(TupleTableSlot *slot, int attnum,
                                  bool *isnull) {
  TRACE("attnum: %d, slot: %s", attnum, slotToString(slot));
  return 0; /* silence compiler warnings */
}

static void tts_trace_getsomeattrs(TupleTableSlot *slot, int natts) {
  TraceTupleTableSlot *tslot = (TraceTupleTableSlot *) slot;
  TRACE("natts: %d, slot: %p %s", natts, slot, slotToString(slot));

  TraceEnsureNoSlotChanges(tslot, DIR_INCOMING);

  slot_getsomeattrs_int(tslot->wrapped, natts);

  slot->tts_flags = tslot->wrapped->tts_flags;
  slot->tts_nvalid = tslot->wrapped->tts_nvalid;
  TraceEnsureNoSlotChanges(tslot, DIR_OUTGOING);
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
