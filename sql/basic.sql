CREATE EXTENSION traceam;
SET client_min_messages TO 'debug2';

CREATE TABLE test (a int, b text) USING traceam;
SELECT * FROM test;

INSERT INTO test VALUES (0, 'A');
SELECT * FROM test;
