#include "traceam.h"

#include <postgres.h>

#include <access/table.h>
#include <catalog/heap.h>
#include <catalog/namespace.h>
#include <catalog/pg_am_d.h>
#include <utils/lsyscache.h>
#include <utils/rel.h>

#include "trace.h"

/* Create inner heap table using the relfilenode.  This is because the
 * relfilenode might change so we should mirror this internally as
 * well. */
static void get_filenode_relname(RelFileNumber relnum, char *relname,
                                 size_t bufsize) {
  snprintf(relname, bufsize, "inner_%u", relnum);
}

void trace_create_filenode(Relation relation, const RelFileLocator *newrlocator,
                           char persistance) {
  char relname[NAMEDATALEN];
  get_filenode_relname(newrlocator->relNumber, relname, persistance);

  TRACE("mapping %s to %s", RelationGetRelationName(relation), relname);
  heap_create_with_catalog(relname,
                           get_namespace_oid(TRACEAM_SCHEMA_NAME, false),
                           /* reltablespace */ newrlocator->spcOid,
                           /* relid */ InvalidOid,
                           /* reltypeid */ InvalidOid,
                           /* reloftypeid */ InvalidOid,
                           relation->rd_rel->relowner,
                           /* accessmtd */ HEAP_TABLE_AM_OID,
                           /* tupdesc */ relation->rd_att,
                           /* cooked_constraints */ NIL,
                           RELKIND_RELATION,
                           persistance,
                           relation->rd_rel->relisshared,
                           RelationIsMapped(relation),
                           ONCOMMIT_NOOP,
                           /* reloptions */ (Datum)0,
                           /* use_user_acl */ false,
                           /* allow_system_table_mods */ false,
                           /* is_internal */ false,
                           /* relrewrite */ InvalidOid,
                           /* typaddress */ NULL);
}

Relation trace_open_filenode(RelFileNumber relnum, LOCKMODE lockmode) {
  char relname[NAMEDATALEN];
  Oid guts_relid;
  get_filenode_relname(relnum, relname, sizeof(relname));
  guts_relid =
      get_relname_relid(relname, get_namespace_oid(TRACEAM_SCHEMA_NAME, false));
  return table_open(guts_relid, lockmode);
}
