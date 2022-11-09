#ifndef TUPLE_H_
#define TUPLE_H_

#include <postgres.h>

#include <executor/tuptable.h>

extern PGDLLIMPORT const TupleTableSlotOps TTSOpsTraceTuple;

extern PGDLLIMPORT char *slotToString(TupleTableSlot *slot);

#endif /* TUPLE_H_ */
