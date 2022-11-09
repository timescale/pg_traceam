#ifndef INNER_H_
#define INNER_H_

#include <postgres.h>

#include <catalog/storage.h>
#include <utils/rel.h>

#define TRACEAM_SCHEMA_NAME "traceam"

void create_guts_for(Relation relation, const RelFileNode* newrnode,
                     char persistance);
Relation open_guts_for_relid(Oid relid, LOCKMODE lockmode);
void close_guts(Relation relation, LOCKMODE lockmode);
void get_guts_relname_for(Oid relfilenode, char* relname, size_t bufsize);

#endif /* INNER_H_ */
