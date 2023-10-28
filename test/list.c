#include <stdio.h>
#include <pthread.h>
#include <stdlib.h>

#include "../list.h"

#define WRITER_INSERTS 1000
#define WRITER_COUNT 16
#define DROPPER_COUNT 16
#define INSERT_COUNT WRITER_INSERTS * WRITER_COUNT

uint32_t rand_data_internal() {
	static uint32_t state = 1;
	return state = (uint64_t)state * 279470273u % 0xfffffffb;
}

static uint32_t random_data[INSERT_COUNT];

void rand_data_init() {
	for (size_t i = 0; i < INSERT_COUNT; i++) {
		random_data[i] = rand_data_internal();
	}
}

uint32_t rand_data() {
	static _Atomic uint32_t idx_next = 0;
	uint32_t idx = atomic_fetch_add_explicit(&idx_next, 1, memory_order_relaxed);
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
	rand_data_init();
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
	printf("Drop got to root: %zu\n", counters.drop_got_to_root);
	printf("Drop mark collisions: %zu\n", counters.drop_mark_collision);
	printf("Drop mark unlinking attempts: %zu\n", counters.drop_mark_unlinking);
	printf("Drop after dropped attempts: %zu\n", counters.drop_after_dropped);
	printf("Drop wait for unlink: %zu\n", counters.drop_wait_for_unlink);
	printf("Drop unexisting attempts: %zu\n", counters.drop_unexisting);
	printf("Drop next is unlinking set fail: %zu\n", counters.drop_next_is_unlinking_set_fail);
	printf("Drop lower bound miss: %zu\n", counters.drop_lower_bound_miss);
	printf("Drop mark dropped attempts (should be 0): %zu\n", counters.drop_mark_dropped);
	printf("Drop collisions (should be 0): %zu\n", counters.drop_collision);
	assert(count == 11728);
	assert(counters.drop_collision == 0);
	assert(counters.drop_mark_dropped == 0);
}
