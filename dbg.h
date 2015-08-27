#ifndef __DEBUG_H
#define __DEBUG_H
#define DEBUG 1
#define LG(fmt, ...) \
  do { if (DEBUG) fprintf(stderr, fmt "\n", ##__VA_ARGS__); } while (0)
#endif
