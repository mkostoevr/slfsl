#include <stdatomic.h>
#include <limits.h>
#include <assert.h>

#define unreachable assert(0)

#ifndef LIST_DATA_COMPARE_F
#define LIST_DATA_COMPARE_F(a, b) (a - b)
#endif

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
	_Atomic size_t drop_got_to_root;
	_Atomic size_t drop_next_is_unlinking_set_fail;
	_Atomic size_t drop_mark_collision;
	_Atomic size_t drop_after_dropped;
	_Atomic size_t drop_wait_for_unlink;
	_Atomic size_t drop_mark_dropped;
	_Atomic size_t drop_mark_unlinking;
	_Atomic size_t drop_unexisting;
	_Atomic size_t drop_collision;
	_Atomic size_t drop_lower_bound_miss;
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
		       !(atom.meta & LIST_RELINKING) &&
		       LIST_DATA_COMPARE_F(atom.next->data, data) < 0) {
			l = atom.next;
			atom = l->atom;
		}
		if (atom.meta & LIST_DROPPED) {
			/*
			 * The item we are currently on has been dropped:
			 * restart.
			 */
			l = root;
			atom = l->atom;
			counters.insert_after_dropped++;
			continue;
		}
		if (atom.meta & LIST_RELINKING) {
			/*
			 * The next item has been dropped: wait until it will
			 * be unlinked.
			 */
			atom = l->atom;
			counters.insert_wait_for_unlink++;
			continue;
		}
		new_item->atom = (struct Atom){0, atom.next};
		new_atom = atom;
		new_atom.meta++;
		new_atom.next = new_item;
		if (!atomic_compare_exchange_strong(&l->atom, &atom, new_atom)) {
			/*
			 * Someone has updated the l->atom (changed its next
			 * pointer or removed the l itself).
			 */
			counters.insert_collision++;
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
		       !(atom.meta & LIST_RELINKING) &&
		       LIST_DATA_COMPARE_F(atom.next->data, value) < 0) {
			l = atom.next;
			atom = l->atom;
		}
	recheck_atom:
		if (atom.next == root) {
			/* The item to drop has been dropped by someone else. */
			counters.drop_got_to_root++;
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
			counters.drop_wait_for_unlink++;
			goto recheck_atom;
		}
		if (LIST_DATA_COMPARE_F(atom.next->data, value) > 0) {
			/*
			 * The item either did not exist or was dropped by
			 * soneone else.
			 */
			counters.drop_unexisting++;
			return 0;
		}
		if (LIST_DATA_COMPARE_F(atom.next->data, value) == 0) {
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
				unreachable;
				counters.drop_mark_dropped++;
				return 0;
			}
			if (next_atom.meta & LIST_RELINKING) {
				/*
				 * Wait until the next's next item will be
				 * unlinked.
				 */
				next_atom = atom.next->atom;
				counters.drop_mark_unlinking++;
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
				unreachable;
				counters.drop_collision++;
				return 0;
			}
			/* TODO: Free the dropped item. */
			return 1;
		}
		/*
		 * The l->atom.next is not the lower bound anymore, go find it.
		 */
		counters.drop_lower_bound_miss++;
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

#undef LIST_DATA_COMPARE_F
#undef LIST_DATA_T
#undef LIST_DROPPED
#undef LIST_RELINKING
