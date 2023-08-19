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

struct List *list_new();
struct List *list_init(struct List *l);
struct List *list_insert(struct List *root, LIST_DATA_T data);
struct List *list_next(struct List *l);

static struct List *list_aquire_item(struct List *item) {
	struct List *expected_next = item->next;
again:
	while (!atomic_compare_exchange_strong(&item->next, &expected_next, NULL)) {
		continue;
	}
	if (expected_next == NULL) {
		/*
		 * Someone has locked the item, let's wait for it to free. As
		 * for optimization we could wait here until the item->next is
		 * not NULL without the expensive compare and exchange above.
		 * But I don't have benchmarks confirming this assumption.
		 *
		 * TODO: Provide something stable and reproducible for this.
		 */
		expected_next = atomic_load(&item->next);
		goto again;
	}
	return expected_next;
}

static void list_release_item(struct List *item, struct List *next) {
	atomic_store(&item->next, next);
}

static struct List *list_item_new(LIST_DATA_T data, struct List *next) {
	struct List *new_item = (struct List *)calloc(sizeof(*new_item), 1);
	if (new_item == NULL) {
		return NULL;
	}
	new_item->next = next;
	new_item->data = data;
	return new_item;
}

struct List *list_next(struct List *l) {
	struct List *result = NULL;
	while (result == NULL) {
		result = atomic_load(&l->next);
	}
	return result;
}

struct List *list_insert(struct List *root, LIST_DATA_T data) {
	struct List *l = root;
	struct List *next = list_next(l);
again:
	while (next != root && next->data < data) {
		l = next;
		next = list_next(l);
	}
	next = list_aquire_item(l);
	/*
	 * We have several possibilities here:
	 * 1. Nothing happened between the first loop finish and the item lock.
	 * 2. Someone has added a new item after the found one.
	 * 3. Someone has removed the item that layed after the one we found.
	 *
	 * TODO: Consider an atomic replace possibility (remove current a and
	 *       create a new with another value in another place).
	 */
	if (/* 1, 2 */ next->data >= data || /* 1, 3 */ next == root) {
		/* TODO: Allocate the new item outside of the lock. */
		struct List *new_item = list_item_new(data, next);
		list_release_item(l, new_item);
		return next;
	}
	/*
	 * We got here in case if someone has added a new item before we have
	 * aquired the l item, and now l->next is not the lower bound anymore.
	 *
	 * So release the aquired item and continue.
	 */
	list_release_item(l, next);
	goto again;
}

struct List *list_init(struct List *l) {
	l->next = l;
	return l;
}

struct List *list_new() {
	struct List *l = (struct List *)calloc(sizeof(*l), 1);
	if (l == NULL) {
		return NULL;
	}
	list_init(l);
	return l;
}

void *writer(void *data) {
	struct List *l = (struct List *)data;
	for (int i = 0; i < 4000; i++) {
		list_insert(l, rand());
	}
	return NULL;
}

int main() {
	struct List *root = list_new();
	/* Try to run a single thread and insert 16x items. */
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
