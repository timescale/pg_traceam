#include "guts.h"

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
void get_guts_relname_for(Oid relfilenode, char *relname, size_t bufsize) {
  snprintf(relname, bufsize, "inner_%u", relfilenode);
}

void create_guts_for(Relation relation, const RelFileNode *newrnode,
                     char persistance) {
  char relname[NAMEDATALEN];
  get_guts_relname_for(newrnode->relNode, relname, persistance);

  TRACE("mapping %s to %s", RelationGetRelationName(relation), relname);
  heap_create_with_catalog(relname,
                           get_namespace_oid(TRACEAM_SCHEMA_NAME, false),
                           /* reltablespace */ newrnode->spcNode,
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

Relation open_guts_for_relid(Oid relid, LOCKMODE lockmode) {
  char relname[NAMEDATALEN];
  Oid guts_relid;
  get_guts_relname_for(relid, relname, sizeof(relname));
  guts_relid =
      get_relname_relid(relname, get_namespace_oid(TRACEAM_SCHEMA_NAME, false));
  return table_open(guts_relid, lockmode);
}

void close_guts(Relation relation, LOCKMODE lockmode) {
  return table_close(relation, lockmode);
}
