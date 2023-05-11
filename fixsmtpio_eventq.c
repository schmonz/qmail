#include "alloc.h"
#include "str.h"
#include "fixsmtpio.h"
#include "fixsmtpio_die.h"

#include "fixsmtpio_eventq.h"

typedef struct node {
  const char *event;
  TAILQ_ENTRY(node) nodes;
} node_t;

typedef TAILQ_HEAD(head_s, node) head_t;

static head_t head;
static int eventq_inited = 0;

static void eventq_init() {
  if (eventq_inited) return;
  TAILQ_INIT(&head);
  eventq_inited++;
}

static node_t *eventq_alloc_node() {
  node_t *e = (node_t *)alloc(sizeof(node_t));
  if (!e) die_nomem(__func__,"alloc");
  return e;
}

static char *eventq_alloc_event(const char *event) {
  char *s = (char *)alloc(sizeof(char) * (1 + str_len(event)));
  if (!s) die_nomem(__func__,"alloc");
  return s;
}

void eventq_put(const char *event) {
  node_t *e;
  eventq_init();
  e = eventq_alloc_node();
  e->event = eventq_alloc_event(event);
  str_copy(e->event,event);
  TAILQ_INSERT_TAIL(&head, e, nodes);
}

const char *eventq_get() {
  const char *event;
  node_t *e;
  if (TAILQ_EMPTY(&head)) {
    event = eventq_alloc_event(EVENT_TIMEOUT);
    str_copy(event,EVENT_TIMEOUT);
  } else {
    e = TAILQ_FIRST(&head);
    event = e->event;
    TAILQ_REMOVE(&head, e, nodes);
    alloc_free(e);
  }

  return event;
}
