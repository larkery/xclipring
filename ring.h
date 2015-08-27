#ifndef __RING_H
#define __RING_H

/*
  This is a ringbuffer, for strings, backed by files in a directory

  The ring has a single pointer to the head element
  
  Examples:

  ring:

      v
  ["hello" "world"]
    
  ring_up()

              v
  ["hello" "world"]

  ring_up()

     v
  ["hello" "world"]

  ring_store("blah")

            v
  ["hello" "blah"]

  ring_store("foo")

                    v
  ["hello" "blah" "foo"]

  x = ring_pos()
  ring_move(-2)

     v
  ["hello" "blah" "foo"]

  ring_shift_head(x);

                          v
  ["hello" "blah" "foo" "hello"]
 */


extern int ring_init(char *path, int count);
extern int ring_store(char *text);
extern char *ring_get();
extern int ring_up();
extern int ring_down();
extern int ring_move(int count);

extern int ring_pos();

/* move the head of the ring to the given position */

extern void ring_shift_head(int pos);

#endif
