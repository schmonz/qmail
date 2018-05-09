#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sysqueue.h"

#include "fixsmtpio_event.h"

void event_put(char *event) {
}

char *event_get() {
  return "fnord";
}

typedef struct node
{
    char c;
    TAILQ_ENTRY(node) nodes;
} node_t;

typedef TAILQ_HEAD(head_s, node) head_t;

static void queue_populate(head_t * head, const char * string)
{
    int c = 0;
    for (c = 0; c < strlen(string); ++c)
    {
        struct node * e = malloc(sizeof(struct node));
        if (e == NULL)
        {
            fprintf(stderr, "malloc failed");
            exit(EXIT_FAILURE);
        }
        e->c = string[c];
        TAILQ_INSERT_TAIL(head, e, nodes);
        e = NULL;
    }
}

static void queue_destroy(head_t * head)
{
    struct node * e = NULL;
    while (!TAILQ_EMPTY(head))
    {
        e = TAILQ_FIRST(head);
        TAILQ_REMOVE(head, e, nodes);
        free(e);
        e = NULL;
    }
}

static void queue_print_forwards(head_t * head)
{
    struct node * e = NULL;
    TAILQ_FOREACH(e, head, nodes)
    {
        printf("%c", e->c);
    }
    printf("\n");
}

static void queue_print_backwards(head_t * head)
{
    struct node * e = NULL;
    TAILQ_FOREACH_REVERSE(e, head, head_s, nodes)
    {
        printf("%c", e->c);
    }
    printf("\n");
}

int main(int argc, char *argv[]) {
    head_t head;
    TAILQ_INIT(&head);

    queue_populate(&head, "Hello World!");

    printf("Forwards: ");
    queue_print_forwards(&head);
    printf("Backwards: ");
    queue_print_backwards(&head);

    queue_destroy(&head);
    queue_print_forwards(&head);

    return EXIT_SUCCESS;
}
