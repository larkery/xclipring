#define _GNU_SOURCE

#include <unistd.h>
#include <getopt.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include "ring.h"
#include "x11.h"

int main(int argc, char *argv[]) {
  // read options, and start up

  char *storage_path = NULL;
  int storage_count = 1000;
  char *selection = "CLIPBOARD";
  int rotate = 0;
  bool run_server = true;
  
  int opt;
  while ((opt = getopt(argc, argv, "r:s:d:c:h")) != -1) {
    switch (opt) {
    case 'r':
      // client command
      rotate = atoi(optarg);
      run_server = false;
      break;
    case 's':
      selection = optarg;
      break;
    case 'd':
      storage_path = optarg;
      break;
    case 'c':
      storage_count = atoi(optarg);
      break;
    case 'h':
    default:
      fprintf(stderr, "Usage: %s [-r <number>] | [-s selection] [-d storage] [-c count]\n"
                      "          -r rotates the ring by given amount, and takes the selection\n"
                      "             (this communicates with another instance running on same X server)\n"
                      "          -s sets the selection to operate on, defaults to CLIPBOARD\n"
                      "          -d sets the storage location, defaults to $HOME/.cache/xclipring/selection\n"
                      "          -c sets the storage count, defaults to 1000\n"
                      "          -h prints this help\n",
              argv[0]);
    }
  }
  
  if (run_server) {
    if (!storage_path) {
      asprintf(&storage_path, "%s/.cache/xclipring/%s", getenv("HOME"), selection);
    }
    if (ring_init(storage_path, storage_count)) {
      exit(1);
    }
    if (x_start_loop(selection)) {
      exit(1);
    }
  } else {
    if (x_ring_rotate(selection, rotate)) {
      exit(1);
    }
  }
  

  exit(0);
}
