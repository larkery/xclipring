#define _GNU_SOURCE

#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <assert.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <stdbool.h>
#include <dirent.h>
#include <stdlib.h>
#include <time.h>
#include <utime.h>

#include "dbg.h"
#include "ring.h"

static char* storage_path;

static int ring_pointer;
static int ring_size;
static int ring_min;
static int ring_max;

static char *ring_head = NULL;

static int mkdirs(char *path) {
  assert(path && *path);
  char* p;
  for (p=strchr(path+1, '/'); p; p=strchr(p+1, '/')) {
    *p='\0';
    if ((mkdir(path, 0700))==-1) {
      if (errno != EEXIST) {
        *p='/';
        return -1;
      }
    }
    *p='/';
  }
  return 0;
}

static int to_ring_pointer(char *str) {
  char *c;
  long int l = strtol(str, &c, 10);
  if (c - str != strlen(str)) return -1;

  return (int) l;
}

static char *read_entry(int entry) {
  FILE *fp;
  char *result = NULL;
  char *name;
  asprintf(&name, "%s/%d", storage_path, entry);
  
  if ((fp = fopen(name, "r"))) {
    fseek(fp, 0L, SEEK_END);
    long s = ftell(fp)+1;
    rewind(fp);
    result = malloc(s);
    if ( result != NULL )
    {
      fread(result, s-1, 1, fp);
      result[s-1] = 0;

      // also touch the file
      struct utimbuf new_times;
      new_times.actime = new_times.modtime = time(NULL);
      utime(name, &new_times);
    }
    fclose(fp);
  }
  
  free(name);
  
  return result;
}

static int write_entry(int head, char *string) {
  FILE *fp;
  char *name;
  asprintf(&name, "%s/%d", storage_path, head);
  
  if ((fp = fopen(name, "w"))) {
    fputs(string, fp);
    fclose(fp);
  } else {
    fprintf(stderr, "cannot write to %s\n", name);
    free(name); // finally!
    return -1;
  }
  
  free(name);

  return 0;
}

static int restore_history(char *storage_path) {
  assert(storage_path && *storage_path);

  DIR *directory = opendir(storage_path);

  if (!directory) {
    return -1;
  }

  struct dirent *entry;
  struct stat attributes;

  long int latest_mtime = 0;

  char *path;

  ring_pointer = 0;
  ring_min = ring_size;
  ring_max = 0;

  bool exists = false;
  
  while ((entry = readdir(directory)) != NULL) {
    int ptr = to_ring_pointer(entry->d_name);
    if (ptr >= 0) {
      if (ptr < ring_min) ring_min = ptr;
      if (ptr > ring_max) ring_max = ptr;
      
      asprintf(&path, "%s/%d", storage_path, ptr);
      stat(path, &attributes);
      free(path);

      long int mtime = attributes.st_mtim.tv_sec;
      if (mtime > latest_mtime) {
        latest_mtime = mtime;
        ring_pointer = ptr;
        exists = true;
      }
    }
  }

  if (ring_min > ring_max) ring_min = ring_max = ring_pointer;

  if (exists) {
    ring_head = read_entry(ring_pointer);
  }

  LG("ring init %d %d %d", ring_min, ring_pointer, ring_max);
  
  return 0;
}

int ring_init(char *path, int count) {
  storage_path = path;
  ring_size = count;
  
  if (mkdirs(storage_path)) {
    fprintf(stderr, "failed in mkdirs %s\n", storage_path);
    return -1;
  }

  if (restore_history(storage_path)) {
    fprintf(stderr, "failed in restore_history from %s\n", storage_path);
    return -1;
  }

  return 0;
}

static void ring_insert() {
  // make a space in the ring
  ring_pointer = (ring_pointer + 1) % ring_size;
  if (ring_pointer > ring_max) ring_max = ring_pointer;
  if (ring_pointer < ring_min) ring_min = ring_pointer;
}

int ring_store(char *text) {
  bool is_append = false;
  bool is_identical = false;

  if (ring_head) {
    int last_len = strlen(ring_head);
    if (strncmp(ring_head, text, last_len) == 0) {
      is_append = true;
      is_identical = (last_len == strlen(text));
    }
  }

  if (is_identical) return 0;
  if (!is_append) {
    ring_insert();
  }
  
  if (ring_head) free(ring_head);
  ring_head = strdup(text);
  return write_entry(ring_pointer, ring_head);
}

char *ring_get() {
  if (ring_head) return ring_head;
  else return "";
}

int ring_up() {
  return ring_move(1);
}

int ring_down() {
  return ring_move(-1);
}

int ring_move(int count) {
  int ring_pointer_before = ring_pointer;
  ring_pointer = (ring_pointer + count) % ring_size;
  if (ring_pointer > ring_max) ring_pointer = ring_min;
  if (ring_pointer < ring_min) ring_pointer = ring_max;

  LG("ring move %d %d %d",
     ring_min, ring_pointer, ring_max);
  
  if (ring_pointer_before == ring_pointer) return -1;
  // no effect
  
  ring_head = read_entry(ring_pointer);
  
  return 0;
}

int ring_pos() {
  return ring_pointer;
}

void ring_shift_head(int pos) {
  ring_pointer = pos % ring_size;

  if (ring_pointer > ring_max) ring_max = ring_pointer;
  if (ring_pointer < ring_min) ring_min = ring_pointer;

  ring_store(ring_get());
}
