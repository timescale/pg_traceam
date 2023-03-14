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
step s1d { DELETE FROM iso_basic WHERE i > 3; }
step s1c { COMMIT; }
teardown { SELECT * FROM iso_basic ORDER BY 1; }

session s2
step s2b1 { BEGIN ISOLATION LEVEL READ COMMITTED; }
step s2b2 { BEGIN ISOLATION LEVEL REPEATABLE READ; }
step s2u { UPDATE iso_basic SET i = i * 2; }
step s2d { DELETE FROM iso_basic WHERE i > 1; }
step s2c { COMMIT; }
teardown { SELECT * FROM iso_basic ORDER BY 1; }

# This is probably overkill; some of these permutations may not contain
# additional useful coverage.

# interleaved inserts
permutation s1b1 s2b1 s1u s2u s1c s2c
permutation s1b1 s2b1 s1u s1c s2u s2c
permutation s1b2 s2b2 s1u s2u s1c s2c
permutation s1b2 s2b2 s1u s1c s2u s2c
permutation s1b1 s2b2 s1u s2u s1c s2c
permutation s1b1 s2b2 s1u s1c s2u s2c
permutation s1b2 s2b1 s1u s2u s1c s2c
permutation s1b2 s2b1 s1u s1c s2u s2c

# interleaved deletes
permutation s1b1 s2b1 s1d s2d s1c s2c
permutation s1b1 s2b1 s1d s1c s2d s2c
permutation s1b2 s2b2 s1d s2d s1c s2c
permutation s1b2 s2b2 s1d s1c s2d s2c

# both
permutation s1b1 s2b1 s1d s2u s1c s2c
permutation s1b1 s2b1 s1d s1c s2u s2c
permutation s1b2 s2b2 s2d s1u s2c s1c
permutation s1b2 s2b2 s2d s2c s1u s1c
permutation s1b1 s2b1 s1u s2d s1c s2c
permutation s1b1 s2b1 s1u s1c s2d s2c
permutation s1b2 s2b2 s2u s1d s2c s1c
permutation s1b2 s2b2 s2u s2c s1d s1c
