#include <stdio.h>
#include <stdlib.h>

struct node {
  long value;
  struct node *next;
};

void print_int(long v) {
  printf("%ld ", v);
  fflush(NULL);
}

int p(long v) { return v & 1; }

struct node *add_element(long value, struct node *next) {
  struct node *n = malloc(sizeof(struct node));
  if (!n)
    abort();
  n->value = value;
  n->next = next;
  return n;
}

void m(struct node *list, void (*func)(long)) {
  if (!list)
    return;
  func(list->value);
  m(list->next, func);
}

struct node *f(struct node *list, struct node *acc, int (*pred)(long)) {
  if (!list)
    return acc;
  if (pred(list->value))
    acc = add_element(list->value, acc);
  return f(list->next, acc, pred);
}

void free_list(struct node *list) {
  if (!list)
    return;
  free_list(list->next);
  free(list);
}

int main(void) {
  long data[] = {4, 8, 15, 16, 23, 42};
  int data_length = sizeof(data) / sizeof(data[0]);

  // build linked list by prepending from the end
  struct node *list = NULL;
  for (int i = data_length; i > 0; i--) {
    list = add_element(data[i - 1], list);
  }
  // Print all
  m(list, print_int);
  puts("");

  // Filter odd elements
  struct node *filtered = f(list, NULL, p);
  m(filtered, print_int);
  puts("");

  // Fix memory leaks
  free_list(list);
  free_list(filtered);
}
