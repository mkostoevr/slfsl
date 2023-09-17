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

#define LIST_DROPPED   (1ULL << 63)
#define LIST_RELINKING (1ULL << 62)

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
struct List *list_next(struct List *root, struct List *it);

static struct List *list_item_new(LIST_DATA_T data) {
	struct List *new_item = (struct List *)calloc(sizeof(*new_item), 1);
	assert(new_item != NULL);
	new_item->data = data;
	return new_item;
}

static struct {
	_Atomic size_t insert_collision;
	_Atomic size_t insert_after_dropped;
	_Atomic size_t insert_wait_for_unlink;
	_Atomic size_t drop_next_is_unlinking_set_fail;
	_Atomic size_t drop_mark_collision;
	_Atomic size_t drop_after_dropped;
	_Atomic size_t drop_mark_dropped;
	_Atomic size_t drop_mark_unlinking;
	_Atomic size_t drop_unexisting;
	_Atomic size_t drop_collision;
} counters;

struct List *list_insert(struct List *root, LIST_DATA_T data) {
	struct List *new_item = list_item_new(data);
	struct List *l = root;
	struct Atom atom = l->atom;
	struct Atom new_atom = {};

	for (;;) {
		/* TODO: 1. We got to node.
		 *       2. The node is deleted.
		 *       3. The we go next.
		 *       4. Between 2 and 3 soneone has added the node that
		 *          satisfies our requirements.
		 *       5. ?
		 */
		while (atom.next != root && !(atom.meta & LIST_DROPPED) &&
		       !(atom.meta & LIST_RELINKING) && atom.next->data < data) {
			l = atom.next;
			atom = l->atom;
		}
		if (atom.meta & LIST_DROPPED) {
			/*
			 * The item we are currently on has been dropped:
			 * restart.
			 */
			counters.insert_after_dropped++;
			l = root;
			atom = l->atom;
			continue;
		}
		if (atom.meta & LIST_RELINKING) {
			/*
			 * The next item has been dropped: wait until it will
			 * be unlinked.
			 */
			counters.insert_wait_for_unlink++;
			atom = l->atom;
			continue;
		}
		new_item->atom = (struct Atom){0, atom.next};
		new_atom = atom;
		new_atom.meta++;
		new_atom.next = new_item;
		if (!atomic_compare_exchange_strong(&l->atom, &atom, new_atom)) {
			counters.insert_collision++;
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

int list_drop(struct List *root, LIST_DATA_T value)
{
	struct List *l = root;
	struct Atom atom = l->atom;

	for (;;) {
		while (atom.next != root && !(atom.meta & LIST_DROPPED) &&
		       !(atom.meta & LIST_RELINKING) && atom.next->data < value) {
			l = atom.next;
			atom = l->atom;
		}
	recheck_atom:
		if (atom.next == root) {
			/* The item to drop has been dropped by someone else. */
			return 0;
		}
		if (atom.meta & LIST_DROPPED) {
			/*
			 * The item we are cucrently on has been dropped. This
			 * is a rare case, so let's just restart. We can't
			 * continue, because it could happen that someone added
			 * a new item just before the l->atom.next, so we could
			 * have found it if we would just start from the
			 * beginning.
			 *
			 * TODO: This is not a legal behaviour for a database
			 *       though, but let's make it this way for now.
			 *
			 * XXX: Think more over this consequences: any more
			 *      reasons we can not continue?
			 */
			l = root;
			atom = l->atom;
			counters.drop_after_dropped++;
			continue;
		}
		if (atom.meta & LIST_RELINKING) {
			/* Wait until the next item will be unlinked. */
			atom = l->atom;
			goto recheck_atom;
		}
		if (atom.next->data > value) {
			/*
			 * The item either did not exist or was dropped by
			 * soneone else.
			 */
			counters.drop_unexisting++;
			return 0;
		}
		if (atom.next->data == value) {
			/*
			 * Mark that the next item is going to be
			 * unlinked in the future. Since then no one is
			 * able to get to the next item from the current
			 * one.
			 */
			struct Atom intermediate_atom = { (atom.meta + 1) | LIST_RELINKING, atom.next };
			if (!atomic_compare_exchange_strong(&l->atom, &atom, intermediate_atom)) {
				/*
				 * Someone has changed the current item
				 * while we attempted to mark that the
				 * next item is to be removed. It's
				 * possible the next item is not the
				 * item to remove anymore, let's check
				 * it.
				 */
				counters.drop_next_is_unlinking_set_fail++;
				goto recheck_atom;
			}
			/* Mark the next item as dropped. */
			struct Atom next_atom = atom.next->atom;
		recheck_next_atom:
			if (next_atom.meta & LIST_DROPPED) {
				/*
				 * No one can drop the item because it belongs
				 * to the current item, which was locked above
				 * by setting the LIST_UNLINKING flag. So no
				 * one can access its next item (neither drop
				 * it nor insert after the current or the next
				 * item).
				 */
				counters.drop_mark_dropped++;
				unreachable;
				return 0;
			}
			if (next_atom.meta & LIST_RELINKING) {
				/*
				 * Wait until the next's next item will be
				 * unlinked.
				 */
				counters.drop_mark_unlinking++;
				next_atom = atom.next->atom;
				goto recheck_next_atom;
			}
			struct Atom new_next_atom = { (next_atom.meta + 1) | LIST_DROPPED, NULL };
			struct List *new_next = next_atom.next;
			if (!atomic_compare_exchange_strong(&atom.next->atom, &next_atom, new_next_atom)) {
				counters.drop_mark_collision++;
				goto recheck_next_atom;
			}
			/*
			 * Now the item is dropped, no one can use it, let's
			 * drop the reference to it from its parent.
			 */
			struct Atom new_atom = { (intermediate_atom.meta + 1) & (~LIST_RELINKING), new_next };
			if (!atomic_compare_exchange_strong(&l->atom, &intermediate_atom, new_atom)) {
				/*
				 * This should not be possible, the current item
				 * is locked above by setting the LIST_UNLINKING
				 * flag.
				 */
				counters.drop_collision++;
				unreachable;
				return 0;
			}
			/* TODO: Free the dropped item. */
			return 1;
		}
		/*
		 * The l->atom.next is not the lower bound anymore, go find it.
		 */
		continue;
	}
	unreachable;
	return 0;
}

struct List *list_init(struct List *l) {
	l->atom = (struct Atom){0, l};
	return l;
}

struct List *list_new() {
	struct List *l = (struct List *)calloc(sizeof(*l), 1);
	assert(l != NULL);
	list_init(l);
	return l;
}

struct List *list_next(struct List *root, struct List *it) {
	_Atomic struct Atom *atom_ptr = &it->atom;
	struct Atom atom;
	do {
	reload:
		atom = atomic_load(atom_ptr);
		if (atom.meta & LIST_DROPPED) {
			/* TODO: Find the lower bound instead. */
			atom_ptr = &root->atom;
			goto reload;
		}
	} while (atom.meta & LIST_RELINKING);
	assert(!(atom.meta & LIST_DROPPED));
	assert(!(atom.meta & LIST_RELINKING));
	assert(atom.next != 0);
	return atom.next;
}

/* THE TEST PROGRAM ***********************************************************/

#define WRITER_INSERTS 1000
#define WRITER_COUNT 16
#define DROPPER_COUNT 16
#define INSERT_COUNT WRITER_INSERTS * WRITER_COUNT

uint32_t rand_data_internal() {
	static uint32_t state = 1;
	return state = (uint64_t)state * 279470273u % 0xfffffffb;
}

uint32_t rand_data() {
	static uint32_t random_data[INSERT_COUNT];
	static _Atomic uint32_t idx_next = 0;
	uint32_t idx = atomic_fetch_add_explicit(&idx_next, 1, memory_order_relaxed);
	if (idx == 0) {
		for (size_t i = 0; i < INSERT_COUNT; i++) {
			random_data[i] = rand_data_internal();
		}
	}
	if (idx >= INSERT_COUNT) {
		printf("INTERNAL ERROR: More writes than expected.\n");
		exit(1);
	}
	return random_data[idx];
}

void *writer(void *data) {
	struct List *l = (struct List *)data;
	for (int i = 0; i < WRITER_INSERTS; i++) {
		list_insert(l, rand_data());
	}
	return NULL;
}

_Atomic int finish = 0;

void *dropper(void *data) {
	struct List *l = (struct List *)data;
	static _Atomic int to_drop = 0;
	int my_to_drop = to_drop++;
	int dropped = 0;
	int done = 0;
redo:
	do {
		dropped = 0;
		for (struct List *it = list_next(l, l); it != l; it = list_next(l, it)) {
			if (it->data % (DROPPER_COUNT * 2) == my_to_drop) {
				list_drop(l, it->data);
				dropped = 1;
			}
		}
	} while (!finish || dropped);
	if (!done) {
		/*
		 * The writes could have finished while we was in the middle of
		 * the list and have inserted something prior our iterator.
		 */
		done = 1;
		goto redo;
	}
	return NULL;
}

int main() {
	struct List *root = list_new();
	pthread_t twriter[WRITER_COUNT];
	pthread_t tdropper[DROPPER_COUNT];
	for (size_t i = 0; i < WRITER_COUNT; i++) {
		pthread_create(&twriter[i], NULL, writer, root);
	}
	for (size_t i = 0; i < DROPPER_COUNT; i++) {
		pthread_create(&tdropper[i], NULL, dropper, root);
	}
	for (size_t i = 0; i < WRITER_COUNT; i++) {
		pthread_join(twriter[i], NULL);
	}
	finish = 1;
	for (size_t i = 0; i < DROPPER_COUNT; i++) {
		pthread_join(tdropper[i], NULL);
	}
	size_t count = 0;
	int prev = INT_MIN;
	for (struct List *it = list_next(root, root); it != root; it = list_next(root, it)) {
		assert(it->data >= prev);
		prev = it->data;
		count++;
	}
	printf("Item count: %zu\n", count);
	printf("Insert collisions: %zu\n", counters.insert_collision);
	printf("Insert after dropped: %zu\n", counters.insert_after_dropped);
	printf("Insert wait for unlink: %zu\n", counters.insert_wait_for_unlink);
	printf("Drop mark collisions: %zu\n", counters.drop_mark_collision);
	printf("Drop mark unlinking attempts: %zu\n", counters.drop_mark_unlinking);
	printf("Drop after dropped attempts: %zu\n", counters.drop_after_dropped);
	printf("Drop unexisting attempts: %zu\n", counters.drop_unexisting);
	printf("Drop next is unlinking set fail: %zu\n", counters.drop_next_is_unlinking_set_fail);
	printf("Drop mark dropped attempts (should be 0): %zu\n", counters.drop_mark_dropped);
	printf("Drop collisions (should be 0): %zu\n", counters.drop_collision);
}
