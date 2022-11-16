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
