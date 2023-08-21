#include <stdio.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <pthread.h>
#include <limits.h>
#include <assert.h>

#define unreachable assert(0)

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

static struct List *list_item_new(LIST_DATA_T data) {
	struct List *new_item = (struct List *)calloc(sizeof(*new_item), 1);
	if (new_item == NULL) {
		return NULL;
	}
	new_item->data = data;
	new_item->next = NULL;
	return new_item;
}

struct List *list_insert(struct List *root, LIST_DATA_T data) {
	struct List *new_item = list_item_new(data);
	struct List *l = root;
	struct List *next = l->next;

	for (;;) {
		while (next != root && next->data < data) {
			l = next;
			next = l->next;
		}
		new_item->next = next;
		/* FIXME: ABA-vulnerable. */
		if (!atomic_compare_exchange_strong(&l->next, &next, new_item)) {
			/*
			 * a) Someone has added a new item after l.
			 * b) Someone has removed the l->next.
			 */
			continue;
		}
		/* We have successfully inserted the new item. */
		return new_item;
	}
	unreachable;
	return root;
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
	int prev = INT_MIN;
	for (struct List *it = root->next; it != root; it = it->next) {
		assert(it->data >= prev);
		prev = it->data;
		count++;
	}
	printf("Item count: %zu\n", count);
}
