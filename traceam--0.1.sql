-- complain if script is sourced in psql, rather than via CREATE EXTENSION
\echo Use "CREATE EXTENSION traceam" to load this file. \quit

CREATE FUNCTION traceam_handler(internal)
RETURNS table_am_handler
AS '$libdir/traceam'
LANGUAGE C;

CREATE ACCESS METHOD traceam TYPE TABLE HANDLER traceam_handler;
COMMENT ON ACCESS METHOD traceam IS 'Table access method tracing calls';
