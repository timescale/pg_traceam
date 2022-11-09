--SET client_min_messages TO DEBUG2;
CREATE TABLE foo(a int) USING traceam;
INSERT INTO foo VALUES (1),(2);
SELECT * FROM foo;

TRUNCATE foo;
SELECT * FROM foo;

