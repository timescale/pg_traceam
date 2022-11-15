/**
 * Trace macros.
 *
 * These are used by the callbacks to emit a trace prefixed with the
 * function that is being called.
 */
#ifndef TRACE_H_
#define TRACE_H_

#define TRACE(FMT, ...) \
  ereport(              \
      DEBUG2,           \
      (errmsg_internal("%s " FMT, __func__, ##__VA_ARGS__), errbacktrace()));

#define TRACE_DETAIL(FMT, ...) \
  ereport(                     \
      DEBUG3,                  \
      (errmsg_internal("%s " FMT, __func__, ##__VA_ARGS__), errbacktrace()));

#endif /* TRACE_H_ */
