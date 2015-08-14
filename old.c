#define _GNU_SOURCE

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>
#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <time.h>
#include <utime.h>

#include <xcb/xcb.h>
#include <xcb/xfixes.h>
#include <xcb/xcb_event.h>

/* settings */
char *selection = "CLIPBOARD";
char *storage   = "%s/.cache/xclipring/%s/";
uint32_t size   = 100;

/* add an entry to storage */
void store_utf8(char *);
/* read the last entry back in (does not set clipboard) */
void load_utf8();

void rotate_down();
void rotate_up();

void run_x_loop();
int x_connect();
int read_options(int, char**);
int mkpath(char* file_path, mode_t mode);

int main(int argc, char *argv[]) {
  int e = 0;

  if ((e = read_options(argc, argv))) exit(e);
  if ((e = x_connect())) exit(e);
  
  run_x_loop();
}

char *default_storage_path() {
  char *result = NULL;
  asprintf(&result, storage, getenv("HOME"), selection);
  return result;
}

int read_options(int argc, char *argv[]) {
  int opt;
  bool set_storage = false;
  while ((opt = getopt(argc, argv, "s:d:c:")) != -1) {
    switch (opt) {
    case 's':
      selection = optarg;
      break;
    case 'd':
      storage = optarg;
      set_storage = true;
      break;
    case 'c':
      size = atoi(optarg);
      break;
    default:
      if (!set_storage) storage = default_storage_path();
      fprintf(stderr,
              "Usage: %s [-s selection] [-d directory] [-c count]\n"
              "          -s sets the selection name, currently %s\n"
              "          -d sets the storage directory, currently %s\n"
              "          -c sets the number of clippings to store, currently %d\n",
              argv[0], selection, storage, size);
      return 1;
    }
  }

  if (!set_storage) storage = default_storage_path();

  // mkdirs for storage
  if (mkpath(storage, 0700)) {
    fprintf(stderr, "%s %s\n",
            strerror(errno),
            storage);
    return errno;
  }
  
  return 0;
}

int mkpath(char* file_path, mode_t mode) {
  assert(file_path && *file_path);
  char* p;
  for (p=strchr(file_path+1, '/'); p; p=strchr(p+1, '/')) {
    *p='\0';
    if ((mkdir(file_path, mode))==-1) {
      if (errno != EEXIST) {
        *p='/';
        return -1;
      }
    }
    *p='/';
  }
  return 0;
}

/* X11 */

#define ATOMS(X)                                \
  X(CLIPBOARD),                                 \
    X(UTF8_STRING),                             \
    X(XSEL_DATA),                               \
    X(NULL_ATOM),                               \
    X(ATOM),                                    \
    X(TARGETS),                                 \
    X(INCR),                                    \
    X(INTEGER)


#define IDENTITY(X) X
#define STRING(X) [X] = #X

enum Atom {
  ATOMS(IDENTITY)
};


char * atom_names[] = {
  ATOMS(STRING)
};

#define LENGTH(x) (sizeof(x)/sizeof(*x))
#define ATOM_COUNT LENGTH(atom_names)
  
xcb_connection_t *X = NULL;
xcb_window_t      window;
xcb_atom_t        atoms[ATOM_COUNT];
const xcb_query_extension_reply_t *xfixes;

int x_get_atoms() {
  xcb_intern_atom_cookie_t cookies[ATOM_COUNT];

  atom_names[CLIPBOARD] = selection;
  atom_names[NULL_ATOM] = "NULL";
  
  for (int i = 0; i<ATOM_COUNT; i++) {
    cookies[i] = xcb_intern_atom(X, 0, strlen(atom_names[i]), atom_names[i]);
  }
  
  for (int i = 0; i<ATOM_COUNT; i++) {
    xcb_intern_atom_reply_t *reply;
    reply = xcb_intern_atom_reply(X, cookies[i], NULL);
    if (reply) {
      atoms[i] = reply->atom;
      free(reply);
    } else {
      fprintf(stderr, "could not get atom %s\n", atom_names[i]);
      return -1;
    }
  }
  
  return 0;
}

int x_check_xfixes() {
  xfixes = xcb_get_extension_data(X, &xcb_xfixes_id);
  if (!xfixes || !xfixes->present) {
    return -1;
  }

  xcb_xfixes_query_version_cookie_t
    xfixes_query_cookie = xcb_xfixes_query_version(X,
                                                   XCB_XFIXES_MAJOR_VERSION,
                                                   XCB_XFIXES_MINOR_VERSION);
  xcb_xfixes_query_version_reply_t
    *xfixes_query = xcb_xfixes_query_version_reply (X,
                                                    xfixes_query_cookie, NULL);

  if (!xfixes_query) {
    fprintf(stderr, "xfixes support missing from server\n");
    return -1;
  }

  if (xfixes_query->major_version < 2) {
    fprintf(stderr, "xfixes version %d too low\n", xfixes_query->major_version);
    free(xfixes_query);
    return -1;
  }

  free(xfixes_query);
  
  return 0;
}

int x_connect() {
  X = xcb_connect(NULL, NULL);

  if (!X) {
    fprintf(stderr, "Could not connect to X server\n");
    return -1;
  }

  // get our atoms

  if (x_get_atoms()) {
    return -1;
  }

  if (x_check_xfixes()) {
    return -1;
  }

  const xcb_setup_t      *setup  = xcb_get_setup (X);
  xcb_screen_iterator_t  iter    = xcb_setup_roots_iterator (setup);
  xcb_screen_t           *screen = iter.data;

  window = xcb_generate_id (X);

  xcb_create_window(X, screen->root_depth, window, screen->root,
                    0, 0, 1, 1, 0, XCB_COPY_FROM_PARENT, screen->root_visual,
                    XCB_CW_BACK_PIXEL | XCB_CW_OVERRIDE_REDIRECT | XCB_CW_EVENT_MASK,
                    (unsigned int[]){screen->black_pixel, 1, XCB_EVENT_MASK_PROPERTY_CHANGE});

  xcb_map_window(X, window);
  
  // register for selection change notifications
  xcb_xfixes_select_selection_input(X, window, atoms[CLIPBOARD],
                                    XCB_XFIXES_SELECTION_EVENT_MASK_SELECTION_CLIENT_CLOSE
                                    | XCB_XFIXES_SELECTION_EVENT_MASK_SELECTION_WINDOW_DESTROY
                                    | XCB_XFIXES_SELECTION_EVENT_MASK_SET_SELECTION_OWNER);

  
  
  xcb_flush(X);

  return 0;
}

xcb_atom_t* best_atom(xcb_atom_t* desired_atoms, uint desired_size,
                      xcb_atom_t* available_atoms, uint available_size) {
  int best_option = -1;

  // for each atom we are offered
  for (int a=0; a<available_size; a++) {
    // for each type that we like
    for (int d=best_option+1; d<desired_size; d++) {
      if (desired_atoms[d] == available_atoms[a]) best_option = d;
      if (best_option+1 == desired_size) return desired_atoms+best_option;
    }
  }

  if (best_option < 0) return NULL;
  return desired_atoms+best_option;
}

void x_selection_converted(xcb_selection_notify_event_t *event) {
  if (event->selection == atoms[CLIPBOARD] &&
      event->property != atoms[NULL_ATOM] &&
      event->property != XCB_NONE) {
    xcb_get_property_cookie_t cookie
      = xcb_get_property(X, 0, // 0 = do not delete, for some reason
                         event->requestor, // who has the clipboard
                         event->property, // the property it is in
                         XCB_GET_PROPERTY_TYPE_ANY, // what kind of thing do we want
                         0L, UINT_MAX); //these last two are the bounds of the property

    xcb_get_property_reply_t *reply = xcb_get_property_reply(X, cookie, 0);

    if (reply) {
      if (reply->type == atoms[ATOM]) {
        // so this is a list of atoms, probably came from a request for targets
        xcb_atom_t *target_atoms = (xcb_atom_t*) xcb_get_property_value(reply);
        xcb_atom_t atoms_we_like[] = {atoms[UTF8_STRING]};
        xcb_atom_t *preferred_type = best_atom(atoms_we_like, 1,
                                               target_atoms, reply->value_len);
              
        if (preferred_type) {
          xcb_convert_selection(X, window,
                                atoms[CLIPBOARD],
                                *preferred_type,
                                atoms[XSEL_DATA],
                                XCB_CURRENT_TIME);
        }
      } else if (reply->type == atoms[UTF8_STRING]) {
        store_utf8((char*) xcb_get_property_value(reply));
      } else if (reply->type == atoms[INCR]) {
        // incremental stuff not implemented
        // https://tronche.com/gui/x/icccm/sec-2.html
        fprintf(stderr, "recvd reply as incr. not sure what to do.\n");
      } else {
        xcb_get_atom_name_reply_t *atomname =
          xcb_get_atom_name_reply(X, xcb_get_atom_name(X, reply->type), NULL);
        if (atomname)  {
          free(atomname);
        } else {
          fprintf(stderr, "getting atom name for atom %d yielded null\n", reply->type);
        }
      }
          
      xcb_delete_property(X, event->requestor, event->property);
      free(reply);
    }
  }
}

void x_take_selection() {
  // ask to have the selection - there is probably a response which says yay
  // that we need to get hold of.
  xcb_set_selection_owner(X, window, atoms[CLIPBOARD], XCB_CURRENT_TIME);
  xcb_flush();
}

// someone wants the selection that we apparently may hold
void x_selection_requested(xcb_selection_request_event_t *e) {
  xcb_selection_notify_event_t ev;
  ev.response_type = XCB_SELECTION_NOTIFY;
  ev.target = e->target;
  ev.requestor = e->requestor;
  ev.property = XCB_NONE; // presume failure
  ev.selection = e->selection;
  ev.time = e->time;
  
  if (e->selection == atoms[CLIPBOARD]) {
    // right selection
    if (e->target == atoms[UTF8_STRING] ||
        e->target == atoms[TEXT] ||
        e->target == atoms[STRING]) {
    } else if (e->target == atoms[INTEGER]) {
      // timestamp of the selection?
      // TODO also should we be posting a selection changed event when we rotate the ring?
      //      after all, we still own the selection but we have changed what we will offer
      // TODO how should we accept invitations to rotate the ring? x events or signals?
      // client messages appear to be the thing here; not sure how we identify the right window.
    } else if (e->target == atoms[TARGETS]) {
      // make targets response (we can do the above options)
      
    }

    if (ok) {
      // we send the data by setting a property on the target window
      xcb_change_property(X, mode, e->requestor, e->property, target, format, size, data);
    }
  }

  // having finished with that we say we are done.
  xcb_send_event(X, 0, ev.requestor, XCB_EVENT_MASK_NO_EVENT, (char*) &ev);
}

void run_x_loop() {
  // the real deal
  xcb_generic_event_t *e;
  while ((e = xcb_wait_for_event(X))) {
    if (!e) return;
    
    uint response_type = e->response_type & ~0x80;
    if (response_type == xfixes->first_event + XCB_XFIXES_SELECTION_NOTIFY) {
      // request clipboard transfer as string
      // TODO check this is not our own selection that we have been notified of
      xcb_convert_selection(X, window,
                            atoms[CLIPBOARD],
                            atoms[TARGETS],
                            atoms[XSEL_DATA],
                            XCB_CURRENT_TIME);
    } else if (XCB_EVENT_RESPONSE_TYPE(e) == XCB_SELECTION_NOTIFY) {
      x_selection_converted((xcb_selection_notify_event_t *) e);
    } else if (XCB_EVENT_RESPONSE_TYPE(e) == XCB_SELECTION_REQUEST) {
      // someone wants us to send them the selection
      // can we just send it back as UTF8
      x_selection_requested((xcb_selection_request_event_t *)e);
    } else if (XCB_EVENT_RESPONSE_TYPE(e) == XCB_SELECTION_CLEAR) {
      // someone else took the selection away, which seems OK
    } else if (XCB_EVENT_RESPONSE_TYPE(e) == 0) {
      xcb_generic_error_t *error = (xcb_generic_error_t *) e;
      fprintf(stderr, "delayed error: %u %u %u\n", error->error_code,
              error->major_code, error->minor_code);
    }

    free(e);
    xcb_flush(X);
  }
}

/* stuff about storing things. 
   we keep only a current thing in memory
   sometimes it is our thing, sometimes it is another process' last thing
 */

char *last_copied_string = NULL;
// the index of last kill
long counter = -1;

//TODO keep top and bottom of counter in sync as well.
void rotate_down() {
  counter = (counter - 1) % size;
}

void rotate_up() {
  counter = (counter + 1) % size;
}

void init_counter() {
  // list the directory finding the most recently affected kill and then add 1 to it
  DIR *directory = opendir(storage);

  struct dirent *entry;
  struct stat attributes;

  long int minmtim = 0;
  
  while ((entry = readdir(directory)) != NULL) {
    char *end;
    long int counter_ = strtol(entry->d_name, &end, 10);
    
    if (end - entry->d_name != strlen(entry->d_name)) continue;
    
    asprintf(&end, "%s/%s", storage, entry->d_name);
    stat(end, &attributes);

    long int mtim = attributes.st_mtim.tv_sec;

    if (mtim > minmtim) {
      minmtim = mtim;
      counter = counter_;
    }
    
    free(end);
  }
  
  closedir(directory);
}

void store_in(long counter, char *string) {
  FILE *fp;
  char *name;
  asprintf(&name, "%s/%ld", storage, counter);
  
  if ((fp = fopen(name, "w"))) {
    fputs(string, fp);
    fclose(fp);
  } else {
    fprintf(stderr, "cannot write to %s\n", name);
  }
  
  free(name);
}

void store_utf8(char *string) {
  bool amend = false;
  bool unaffected = false;

  if (counter < 0) {
    // find most recent thing in dir
    init_counter();
    // read it in, if it exists.
    load_utf8();
  }
  
  int len_last_copy = last_copied_string ? strlen(last_copied_string) : 0;
  
  if (last_copied_string && strncmp(last_copied_string, string, len_last_copy) == 0) {
    // this is a prefix, so we want to amend the current kill
    amend = true;
    unaffected = (len_last_copy == strlen(string));
  }
  
  if (!unaffected) {
    if (last_copied_string) free(last_copied_string);
    last_copied_string = strdup(string);

    if (!amend) {
      rotate_up();
    }
    
    store_in(counter, last_copied_string);
  }
}

void load_utf8() {
  FILE *fp;
  char *name;
  asprintf(&name, "%s/%ld", storage, counter);
  
  if ((fp = fopen(name, "r"))) {
    fseek(fp, 0L, SEEK_END);
    long s = ftell(fp)+1;
    rewind(fp);
    char *buffer = malloc(s);
    if ( buffer != NULL )
    {
      fread(buffer, s-1, 1, fp);
      buffer[s-1] = 0;
      if (last_copied_string) free(last_copied_string);
      last_copied_string = buffer;

      // also touch the file
      struct utimbuf new_times;
      new_times.actime = new_times.modtime = time(NULL);
      utime(name, &new_times);

      // set the selection
      x_take_selection(last_copied_string);
    }
    fclose(fp);
  } else {
    fprintf(stderr, "cannot write to %s\n", name);
  }
  
  free(name);
}
