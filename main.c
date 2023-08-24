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

struct List;

struct Atom {
	size_t meta;
	struct List *next;
};

struct List {
	LIST_DATA_T data;
	_Atomic struct Atom atom;
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
	return new_item;
}

_Atomic size_t collisions = 0;

struct List *list_insert(struct List *root, LIST_DATA_T data) {
	struct List *new_item = list_item_new(data);
	struct List *l = root;
	struct Atom atom = l->atom;
	struct Atom new_atom = {};

	for (;;) {
		/* TODO: Aliveness check. */
		while (atom.next != root && atom.next->data < data) {
			l = atom.next;
			atom = l->atom;
		}
		new_item->atom = (struct Atom){0, atom.next};
		new_atom = atom;
		new_atom.meta++;
		new_atom.next = new_item;
		if (!atomic_compare_exchange_strong(&l->atom, &atom, new_atom)) {
			collisions++;
			/*
			 * Someone has updated the l->atom (changed its next
			 * pointer). TODO: Or removed the l itself.
			 */
			continue;
		}
		/* We have successfully inserted the new item. */
		return new_item;
	}
	unreachable;
	free(new_item);
	return root;
}

struct List *list_init(struct List *l) {
	l->atom = (struct Atom){0, l};
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

struct List *list_next(struct List *l) {
	struct Atom tmp = atomic_load(&l->atom);
	return tmp.next;
}

void *writer(void *data) {
	struct List *l = (struct List *)data;
	for (int i = 0; i < 4000; i++) {
		list_insert(l, rand() % 3);
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
	for (struct List *it = list_next(root); it != root; it = list_next(it)) {
		assert(it->data >= prev);
		prev = it->data;
		count++;
	}
	printf("Item count: %zu\n", count);
	printf("Collisions: %zu\n", collisions);
}
