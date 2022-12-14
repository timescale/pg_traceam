#ifndef TUPLE_H_
#define TUPLE_H_

#include <postgres.h>

#include <executor/tuptable.h>

extern PGDLLIMPORT const TupleTableSlotOps TTSOpsTraceTuple;

typedef struct TraceTupleTableSlot {
  TupleTableSlot base;

  /*
   * Intuitively it seems like we should be able to us a buffer heap slot as
   * our slot's base, essentially "inheriting" from BufferHeapTupleTableSlot.
   * Unfortunately, the heap AM does explicit type checks on its slot using the
   * associated tts_ops field. So we have to wrap the slot instead, and
   * explicitly construct it with the expected tts_ops.
   */
  TupleTableSlot *wrapped;
} TraceTupleTableSlot;

/* for TraceEnsureNoSlotChanges */
#define DIR_INCOMING 0
#define DIR_OUTGOING 1

extern PGDLLIMPORT char *slotToString(TupleTableSlot *slot);
extern PGDLLIMPORT void TraceEnsureNoSlotChanges(TraceTupleTableSlot *tslot, int direction);

#endif /* TUPLE_H_ */
