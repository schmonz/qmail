#include "alloc.h"
#include "str.h"
#include "sysqueue.h"
#include "fixsmtpio_common.h"

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
  if (!e) die_nomem();
  return e;
}

static char *eventq_alloc_event(const char *event) {
  char *s = (char *)alloc(sizeof(char) * (1 + str_len(event)));
  if (!s) die_nomem();
  return s;
}

void eventq_put(const char *event) {
  node_t *e;
  eventq_init();
  e = eventq_alloc_node();
  e->event = event;
  TAILQ_INSERT_TAIL(&head, e, nodes);
}

char *eventq_get() {
  char *event;
  node_t *e;
  if (TAILQ_EMPTY(&head)) {
    event = "";
  } else {
    e = TAILQ_FIRST(&head);
    event = eventq_alloc_event(e->event);
    str_copy(event,e->event);
    TAILQ_REMOVE(&head, e, nodes);
    alloc_free(e);
  }

  return event;
}
