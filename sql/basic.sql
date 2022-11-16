CREATE EXTENSION traceam;
SET client_min_messages TO 'debug2';

CREATE TABLE test (a int, b text) USING traceam;
SELECT * FROM test;

INSERT INTO test VALUES (0, 'A');
SELECT * FROM test;

UPDATE test SET a = 1;
SELECT * FROM test;

-- invoke TOAST
ALTER TABLE test ALTER COLUMN b SET STORAGE EXTERNAL;
INSERT INTO test VALUES(2, repeat('0123456789', 269));
SELECT b = repeat('0123456789', 269) FROM test WHERE a = 2;

-- system columns
INSERT INTO test VALUES(3, 'B')
  RETURNING txid_current() AS txid
\gset
SELECT tableoid = 'test'::regclass, xmin = :txid, a FROM test;

-- bulk load
COPY test FROM STDIN;
4	C
5	D
\.
SELECT * FROM test WHERE a != 2;
