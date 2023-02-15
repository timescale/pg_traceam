setup
{
    DROP TABLE IF EXISTS iso_basic;
    CREATE TABLE iso_basic USING traceam
              AS SELECT i FROM generate_series(1, 5) i;
}

teardown
{
    DROP TABLE iso_basic;
}

session s1
step s1b1 { BEGIN ISOLATION LEVEL READ COMMITTED; }
step s1b2 { BEGIN ISOLATION LEVEL REPEATABLE READ; }
step s1u { UPDATE iso_basic SET i = i * 3; }
step s1c { COMMIT; }
teardown { SELECT * FROM iso_basic; }

session s2
step s2b1 { BEGIN ISOLATION LEVEL READ COMMITTED; }
step s2b2 { BEGIN ISOLATION LEVEL REPEATABLE READ; }
step s2u { UPDATE iso_basic SET i = i * 2; }
step s2c { COMMIT; }
teardown { SELECT * FROM iso_basic; }

# This is probably overkill; some of these permutations may not contain
# additional useful coverage.
permutation s1b1 s2b1 s1u s2u s1c s2c
permutation s1b1 s2b1 s1u s1c s2u s2c
permutation s1b2 s2b2 s1u s2u s1c s2c
permutation s1b2 s2b2 s1u s1c s2u s2c
permutation s1b1 s2b2 s1u s2u s1c s2c
permutation s1b1 s2b2 s1u s1c s2u s2c
permutation s1b2 s2b1 s1u s2u s1c s2c
permutation s1b2 s2b1 s1u s1c s2u s2c
