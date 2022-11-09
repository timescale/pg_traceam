#include "tuple.h"

#include <postgres.h>

#include <inttypes.h>

#include <executor/tuptable.h>
#include <lib/stringinfo.h>
#include <nodes/memnodes.h>

typedef struct TraceTupleTableSlot {
  TupleTableSlot base;
} TraceTupleTableSlot;

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
  elog(DEBUG2, "slot: %p", slot);
}

static void tts_trace_release(TupleTableSlot *slot) {
  elog(DEBUG2, "slot: %p %s", slot, slotToString(slot));
}

static void tts_trace_clear(TupleTableSlot *slot) {
  elog(DEBUG2, "slot: %p %s", slot, slotToString(slot));
  if (unlikely(TTS_SHOULDFREE(slot)))
    slot->tts_flags &= ~TTS_FLAG_SHOULDFREE;

  slot->tts_nvalid = 0;
  slot->tts_flags |= TTS_FLAG_EMPTY;
  ItemPointerSetInvalid(&slot->tts_tid);
}

static void tts_trace_materialize(TupleTableSlot *slot) {
  elog(DEBUG2, "slot: %p %s", slot, slotToString(slot));
}

static void tts_trace_copyslot(TupleTableSlot *dstslot,
                               TupleTableSlot *srcslot) {
  TupleDesc srcdesc = srcslot->tts_tupleDescriptor;
  elog(DEBUG2, "dstslot: %p, srcslot: %p %s", dstslot, srcslot,
       slotToString(srcslot));

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
  elog(DEBUG2, "attnum: %d, slot: %s", attnum, slotToString(slot));
  return 0; /* silence compiler warnings */
}

static void tts_trace_getsomeattrs(TupleTableSlot *slot, int natts) {
  elog(DEBUG2, "natts: %d, slot: %s", natts, slotToString(slot));
}

static HeapTuple tts_trace_copy_heap_tuple(TupleTableSlot *slot) {
  elog(DEBUG2, "slot: %s", slotToString(slot));

  Assert(!TTS_EMPTY(slot));

  return heap_form_tuple(slot->tts_tupleDescriptor, slot->tts_values,
                         slot->tts_isnull);
}

static MinimalTuple tts_trace_copy_minimal_tuple(TupleTableSlot *slot) {
  elog(DEBUG2, "slot: %s", slotToString(slot));

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
