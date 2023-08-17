#include <stdio.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <pthread.h>

#ifndef LIST_DATA_T
#define LIST_DATA_T int
#endif

struct List {
	LIST_DATA_T data;
	struct List *_Atomic next;
};

/*
 * Locks the list item and returns pointer to the next item.
 */
static struct List *list_aquire_item(struct List *item) {
	struct List *expected_next = item->next;
again:
	while (!atomic_compare_exchange_strong(&item->next, &expected_next, NULL)) {
		continue;
	}
	if (expected_next == NULL) {
		/* Someone has locked the item, let's wait for it to free. */
		expected_next = atomic_load(&item->next);
		goto again;
	}
	return expected_next;
}

/*
 * Gives pointer to the next list item.
 */
struct List *list_next(struct List *l) {
	struct List *result = NULL;
	while (result == NULL) {
		result = atomic_load(&l->next);
	}
	return result;
}

/*
 * Sets the item's next (so unlocks it).
 */
void list_release_item(struct List *item, struct List *next) {
	atomic_store(&item->next, next);
}

/*
 * Inserts a new item with the given data after the given item.
 */
struct List *list_insert_after(struct List *after, LIST_DATA_T data) {
	struct List *new_item = calloc(sizeof(*new_item), 1);
	if (new_item == NULL) {
		return NULL;
	}
	new_item->next = list_aquire_item(after);
	new_item->data = data;
	list_release_item(after, new_item);
	return new_item;
}

struct List *list_init(struct List *l) {
	l->next = l;
	return l;
}

struct List *list_new() {
	struct List *l = calloc(sizeof(*l), 1);
	if (l == NULL) {
		return NULL;
	}
	list_init(l);
	return l;
}

void *writer(void *data) {
	struct List *l = data;
	for (int i = 0; i < 1000000; i++) {
		list_insert_after(l, i);
	}
	return NULL;
}

int main() {
	struct List *root = list_new();
	pthread_t th[16];
	for (size_t i = 0; i < (sizeof(th) / sizeof(th[0])); i++) {
		pthread_create(&th[i], NULL, writer, root);
	}
	for (size_t i = 0; i < (sizeof(th) / sizeof(th[0])); i++) {
		pthread_join(th[i], NULL);
	}
	size_t count = 0;
	for (struct List *it = root->next; it != root; it = list_next(it)) {
		count++;
	}
	printf("Item count: %zu\n", count);
}
