#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <limits.h>
#include <sys/types.h>
#include <unistd.h>

#include <xcb/xcb.h>
#include <xcb/xfixes.h>
#include <xcb/xcb_event.h>

#include "x11.h"
#include "ring.h"

#define ATOMS(X)                                \
  X(CLIPBOARD),                                 \
    X(UTF8_STRING),                             \
    X(XSEL_DATA),                               \
    X(NULL_ATOM),                               \
    X(ATOM),                                    \
    X(TARGETS),                                 \
    X(INCR),                                    \
    X(INTEGER),                                 \
    X(CARDINAL),                                \
    X(STRING),                                  \
    X(TEXT),                                    \
    X(XCLIPRING)

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

static xcb_connection_t            *X = NULL;
static xcb_window_t                 window;
static xcb_atom_t                   atoms[ATOM_COUNT];
const  xcb_query_extension_reply_t *xfixes;

static int load_atoms(char * selection_name) {
  xcb_intern_atom_cookie_t cookies[ATOM_COUNT];

  atom_names[CLIPBOARD] = selection_name;
  atom_names[NULL_ATOM] = "NULL";
  asprintf(&(atom_names[XCLIPRING]), "XCLIPRING-%s", selection_name);
  
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

static int init_xfixes() {
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

static int create_window() {
  const xcb_setup_t      *setup  = xcb_get_setup (X);
  xcb_screen_iterator_t  iter    = xcb_setup_roots_iterator (setup);
  xcb_screen_t           *screen = iter.data;

  window = xcb_generate_id (X);

  xcb_create_window(X, screen->root_depth, window, screen->root,
                    0, 0, 1, 1, 0, XCB_COPY_FROM_PARENT, screen->root_visual,
                    XCB_CW_BACK_PIXEL | XCB_CW_OVERRIDE_REDIRECT | XCB_CW_EVENT_MASK,
                    (unsigned int[]){screen->black_pixel, 1, XCB_EVENT_MASK_PROPERTY_CHANGE});

  xcb_map_window(X, window);
  
  xcb_flush(X);

  return 0;
}

static int listen_for_change() {
  if (init_xfixes()) return -1;

  // register for selection change notifications
  xcb_xfixes_select_selection_input(X, window, atoms[CLIPBOARD],
                                    XCB_XFIXES_SELECTION_EVENT_MASK_SELECTION_CLIENT_CLOSE
                                    | XCB_XFIXES_SELECTION_EVENT_MASK_SELECTION_WINDOW_DESTROY
                                    | XCB_XFIXES_SELECTION_EVENT_MASK_SET_SELECTION_OWNER);

  xcb_flush(X);
  
  return 0;
}

static int init_x(char *selection_name) {
  X = xcb_connect(NULL, NULL);

  if (!X) {
    fprintf(stderr, "Could not connect to X server\n");
    return -1;
  }
   
  if (load_atoms(selection_name)) return -1;
  if (create_window()) return -1;

  return 0;
}

static void handle_selection_converted(xcb_selection_notify_event_t *event);
static void handle_selection_requested(xcb_selection_request_event_t *event);

int x_start_loop(char *selection_name) {
  if (init_x(selection_name)) return -1;
  if (listen_for_change()) return -1;

  xcb_generic_event_t *e;
  
  // take selection of our atom
  xcb_set_selection_owner(X, window, atoms[XCLIPRING], XCB_CURRENT_TIME);
  xcb_flush(X);
  
  while ((e = xcb_wait_for_event(X))) {
    if (!e) return 0;
    
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
      handle_selection_converted((xcb_selection_notify_event_t *) e);
    } else if (XCB_EVENT_RESPONSE_TYPE(e) == XCB_SELECTION_REQUEST) {
      // someone wants us to send them the selection
      // can we just send it back as UTF8
      handle_selection_requested((xcb_selection_request_event_t *)e);
    } else if (XCB_EVENT_RESPONSE_TYPE(e) == XCB_SELECTION_CLEAR) {
      // someone else took the selection away, which seems OK
    } else if (XCB_EVENT_RESPONSE_TYPE(e) == 0) {
      xcb_generic_error_t *error = (xcb_generic_error_t *) e;
      fprintf(stderr, "delayed error: %u %u %u\n", error->error_code,
              error->major_code, error->minor_code);
    } else if (XCB_EVENT_RESPONSE_TYPE(e) == XCB_PROPERTY_NOTIFY) {
      xcb_property_notify_event_t *ev = (xcb_property_notify_event_t*)e;
      if (ev->atom == atoms[XCLIPRING]) {
        // rotate clip ring a bit.
        xcb_get_property_cookie_t cookie =
          xcb_get_property(X, 0, window,
                           atoms[XCLIPRING],
                           atoms[CARDINAL],
                           0, 1);
        xcb_get_property_reply_t *reply;
        if ((reply = xcb_get_property_reply(X, cookie, NULL)) &&
            reply->value_len) {
          int amount = *((int *)xcb_get_property_value(reply));
          fprintf(stderr, "rotate %d\n", amount);
          free(reply);
        }
      }
    } else {
      fprintf(stderr, "another event\n");
    }

    // TODO handle selection gone away errors and take selection in that case as well.
    
    free(e);
    xcb_flush(X);
  }
  
  return 0;
}

static void take_selection() {
  // ask to have the selection - there is probably a response which says yay
  // that we need to get hold of.
  xcb_set_selection_owner(X, window, atoms[CLIPBOARD], XCB_CURRENT_TIME);
  xcb_flush(X);
}

int x_ring_rotate(char *selection_name, int count) {
  if (count == 0) return 0;

  if (init_x(selection_name)) return -1;
  
  xcb_get_selection_owner_cookie_t cookie = xcb_get_selection_owner(X, atoms[XCLIPRING]);

  xcb_flush(X);
  
  xcb_generic_error_t *e;
  
  xcb_get_selection_owner_reply_t *reply =
    xcb_get_selection_owner_reply(X, cookie, &e);

  if (e) {
    fprintf(stderr, "error getting owner of XCLIPRING selection\n");
    free(e);
    return -1;
  }

  if (reply) {
    if (reply->owner == 0) {
      fprintf(stderr, "xclipring not running for %s\n", selection_name);
      return -1;
    }

    // send client message to rotate ring
    xcb_change_property(X, XCB_PROP_MODE_REPLACE, reply->owner,
                        atoms[XCLIPRING], atoms[CARDINAL], 32, 1, &count);

    fprintf(stderr, "%d\n", count);
    
    xcb_flush(X);
    
    free(reply);
  }
  
  return 0;
}

// someone wants the selection that we apparently may hold
static void handle_selection_requested(xcb_selection_request_event_t *e) {
  xcb_selection_notify_event_t ev;
  ev.response_type = XCB_SELECTION_NOTIFY;
  ev.target = e->target;
  ev.requestor = e->requestor;
  ev.property = XCB_NONE; // presume failure
  ev.selection = e->selection;
  ev.time = e->time;

  bool ok = false;
  
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
      //xcb_change_property(X, mode, e->requestor, e->property, target, format, size, data);
    }
  }

  // having finished with that we say we are done.
  xcb_send_event(X, 0, ev.requestor, XCB_EVENT_MASK_NO_EVENT, (char*) &ev);
}

static xcb_atom_t* best_atom(xcb_atom_t* desired_atoms, uint desired_size,
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

static void handle_selection_converted(xcb_selection_notify_event_t *event) {
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
        ring_store((char*) xcb_get_property_value(reply));
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
