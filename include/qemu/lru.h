/*
 * LRU object list
 *
 * Copyright (c) 2021-2024 Matt Borgerson
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */
#ifndef LRU_H
#define LRU_H

#include <assert.h>
#include <stdint.h>
#include "qemu/queue.h"

#define LRU_NUM_BINS (1U << 16)
/* Preserve the logical bin mapping while allocating bin heads in sparse pages. */
#define LRU_BIN_BLOCK_BITS 4
#define LRU_BINS_PER_BLOCK (1U << LRU_BIN_BLOCK_BITS)
#define LRU_NUM_BIN_BLOCKS (LRU_NUM_BINS / LRU_BINS_PER_BLOCK)

typedef struct LruNode {
	QTAILQ_ENTRY(LruNode) next_global;
	QTAILQ_ENTRY(LruNode) next_bin;
	uint64_t hash;
} LruNode;

typedef QTAILQ_HEAD(LruBin, LruNode) LruBin;

typedef struct Lru Lru;

enum {
	LRU_LOOKUP_ALLOW_EVICT = 1U << 0,
};

struct Lru {
	QTAILQ_HEAD(, LruNode) global;
	LruBin **bin_blocks;
	int num_used;
	int num_free;

	/* Initialize a node. */
	void (*init_node)(Lru *lru, LruNode *node, const void *key);

	/* In case of hash collision. Return `true` if nodes differ. */
	bool (*compare_nodes)(Lru *lru, LruNode *node, const void *key);

	/* Optional. Called before eviction. Return `false` to prevent eviction. */
	bool (*pre_node_evict)(Lru *lru, LruNode *node);

	/* Optional. Called after eviction. Reclaim any associated resources. */
	void (*post_node_evict)(Lru *lru, LruNode *node);
};

static inline
void lru_init(Lru *lru)
{
	QTAILQ_INIT(&lru->global);
	lru->bin_blocks = NULL;
	lru->init_node = NULL;
	lru->compare_nodes = NULL;
	lru->pre_node_evict = NULL;
	lru->post_node_evict = NULL;
	lru->num_free = 0;
	lru->num_used = 0;
}

static inline
LruBin *lru_get_bin(Lru *lru, unsigned int bin, bool create)
{
	assert(bin < LRU_NUM_BINS);

	if (!lru->bin_blocks) {
		if (!create) {
			return NULL;
		}
		lru->bin_blocks = g_new0(LruBin *, LRU_NUM_BIN_BLOCKS);
	}

	unsigned int block = bin >> LRU_BIN_BLOCK_BITS;
	unsigned int offset = bin & (LRU_BINS_PER_BLOCK - 1);
	LruBin *bins = lru->bin_blocks[block];

	if (!bins) {
		if (!create) {
			return NULL;
		}
		bins = g_new0(LruBin, LRU_BINS_PER_BLOCK);
		for (unsigned int i = 0; i < LRU_BINS_PER_BLOCK; i++) {
			QTAILQ_INIT(&bins[i]);
		}
		lru->bin_blocks[block] = bins;
	}

	return &bins[offset];
}

static inline
void lru_release_bin_storage(Lru *lru)
{
	assert(lru->num_used == 0);

	if (!lru->bin_blocks) {
		return;
	}

	for (unsigned int i = 0; i < LRU_NUM_BIN_BLOCKS; i++) {
		g_free(lru->bin_blocks[i]);
	}
	g_free(lru->bin_blocks);
	lru->bin_blocks = NULL;
}

static inline
void lru_add_free(Lru *lru, LruNode *node)
{
	node->next_bin.tqe_circ.tql_prev = NULL;
	QTAILQ_INSERT_TAIL(&lru->global, node, next_global);
	lru->num_free += 1;
}

static inline
unsigned int lru_hash_to_bin(Lru *lru, uint64_t hash)
{
	return hash % LRU_NUM_BINS;
}

static inline
unsigned int lru_get_node_bin(Lru *lru, LruNode *node)
{
	return lru_hash_to_bin(lru, node->hash);
}

static inline
bool lru_is_node_in_use(Lru *lru, LruNode *node)
{
	return QTAILQ_IN_USE(node, next_bin);
}

static inline
void lru_evict_node(Lru *lru, LruNode *node)
{
	if (!lru_is_node_in_use(lru, node)) {
		return;
	}

	unsigned int bin = lru_get_node_bin(lru, node);
	LruBin *bin_head = lru_get_bin(lru, bin, false);
	assert(bin_head != NULL);
	QTAILQ_REMOVE(bin_head, node, next_bin);
	if (lru->post_node_evict) {
		lru->post_node_evict(lru, node);
	}

	lru->num_used -= 1;
	lru->num_free += 1;
}

static inline
LruNode *lru_try_evict_one(Lru *lru)
{
	LruNode *found;

	QTAILQ_FOREACH_REVERSE(found, &lru->global, next_global) {
		if (lru_is_node_in_use(lru, found)
			&& (!lru->pre_node_evict || lru->pre_node_evict(lru, found))) {
			lru_evict_node(lru, found);
			return found;
		}
	}

	return NULL;
}

static inline
LruNode *lru_evict_one(Lru *lru)
{
	LruNode *found = lru_try_evict_one(lru);

	assert(found != NULL); /* No evictable node! */

	return found;
}

static inline
LruNode *lru_get_one_free(Lru *lru)
{
	LruNode *found;

	QTAILQ_FOREACH_REVERSE(found, &lru->global, next_global) {
		if (!lru_is_node_in_use(lru, found)) {
			return found;
		}
	}

	return lru_evict_one(lru);
}

static inline
bool lru_contains_hash(Lru *lru, uint64_t hash)
{
	unsigned int bin = lru_hash_to_bin(lru, hash);
	LruBin *bin_head = lru_get_bin(lru, bin, false);
	LruNode *iter;

	if (!bin_head) {
		return false;
	}

	QTAILQ_FOREACH(iter, bin_head, next_bin) {
		if (iter->hash == hash) {
			return true;
		}
	}

	return false;
}

static inline
bool lru_contains_key(Lru *lru, uint64_t hash, const void *key)
{
	unsigned int bin = lru_hash_to_bin(lru, hash);
	LruBin *bin_head = lru_get_bin(lru, bin, false);
	LruNode *iter;

	if (!bin_head) {
		return false;
	}

	QTAILQ_FOREACH(iter, bin_head, next_bin) {
		if (iter->hash == hash && !lru->compare_nodes(lru, iter, key)) {
			return true;
		}
	}

	return false;
}

static inline
bool lru_try_lookup(Lru *lru, uint64_t hash, const void *key,
                    unsigned int flags, LruNode **out)
{
	unsigned int bin = lru_hash_to_bin(lru, hash);
	LruBin *bin_head = lru_get_bin(lru, bin, false);
	LruNode *iter, *found = NULL;

	assert(out);
	*out = NULL;

	if (bin_head) {
		QTAILQ_FOREACH(iter, bin_head, next_bin) {
			if ((iter->hash == hash) &&
			    !lru->compare_nodes(lru, iter, key)) {
				found = iter;
				break;
			}
		}
	}

	if (found) {
		QTAILQ_REMOVE(bin_head, found, next_bin);
	} else {
		QTAILQ_FOREACH_REVERSE(found, &lru->global, next_global) {
			if (!lru_is_node_in_use(lru, found)) {
				break;
			}
		}
		if (!found && (flags & LRU_LOOKUP_ALLOW_EVICT)) {
			found = lru_try_evict_one(lru);
		}
		if (!found) {
			return false;
		}

		found->hash = hash;
		if (lru->init_node) {
			lru->init_node(lru, found, key);
		}
		assert(found->hash == hash);

		lru->num_used += 1;
		lru->num_free -= 1;
	}
	if (!bin_head) {
		bin_head = lru_get_bin(lru, bin, true);
	}

	QTAILQ_REMOVE(&lru->global, found, next_global);
	QTAILQ_INSERT_HEAD(&lru->global, found, next_global);
	QTAILQ_INSERT_HEAD(bin_head, found, next_bin);

	*out = found;
	return true;
}

static inline
LruNode *lru_lookup(Lru *lru, uint64_t hash, const void *key)
{
	LruNode *found = NULL;
	bool success = lru_try_lookup(lru, hash, key, LRU_LOOKUP_ALLOW_EVICT,
	                              &found);

	assert(success && found);
	return found;
}

static inline
void lru_flush(Lru *lru)
{
	LruNode *iter, *iter_next;

	for (unsigned int block = 0; block < LRU_NUM_BIN_BLOCKS; block++) {
		LruBin *bins = lru->bin_blocks ? lru->bin_blocks[block] : NULL;
		if (!bins) {
			continue;
		}
		for (unsigned int bin = 0; bin < LRU_BINS_PER_BLOCK; bin++) {
			QTAILQ_FOREACH_SAFE(iter, &bins[bin], next_bin, iter_next) {
				bool can_evict = true;
				if (lru->pre_node_evict) {
					can_evict = lru->pre_node_evict(lru, iter);
				}
				if (can_evict) {
					lru_evict_node(lru, iter);
					QTAILQ_REMOVE(&lru->global, iter, next_global);
					QTAILQ_INSERT_TAIL(&lru->global, iter,
					                   next_global);
				}
			}
		}
	}

	if (!lru->num_used) {
		lru_release_bin_storage(lru);
	}
}

typedef void (*LruNodeVisitorFunc)(Lru *lru, LruNode *node, void *opaque);

static inline
void lru_visit_active(Lru *lru, LruNodeVisitorFunc visitor_func, void *opaque)
{
    LruNode *iter, *iter_next;

    QTAILQ_FOREACH_SAFE(iter, &lru->global, next_global, iter_next) {
        if (lru_is_node_in_use(lru, iter)) {
            visitor_func(lru, iter, opaque);
        }
    }
}

#endif
