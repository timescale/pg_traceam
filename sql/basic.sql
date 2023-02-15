--SET client_min_messages TO DEBUG3;
CREATE TABLE foo(a int) USING traceam;
INSERT INTO foo VALUES (1),(2);
SELECT * FROM foo;

UPDATE foo SET a = a * 2 WHERE a > 1;
SELECT * FROM foo;

-- This will use the transactional version of truncate.
TRUNCATE foo;
SELECT * FROM foo;

START TRANSACTION;
CREATE TABLE bar(a int) using traceam;
SELECT * FROM bar;
INSERT INTO bar VALUES (1),(2);
SELECT * FROM bar;
-- This will use the non-transactional version of truncate since we
-- created the relation in the transaction.
TRUNCATE bar;
SELECT * FROM bar;
COMMIT;
