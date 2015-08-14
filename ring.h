#ifndef __RING_H
#define __RING_H

extern int ring_init(char *path, int count);
extern int ring_store(char *text);
extern char *ring_get();
extern int ring_up();
extern int ring_down();
extern int ring_move(int count);

#endif
