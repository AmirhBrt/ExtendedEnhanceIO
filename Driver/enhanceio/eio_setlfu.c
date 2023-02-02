/*
 * Created by AmirHBrt on 2/2/23.
 * eio_setlfu.c
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; under version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 */

#include "eio.h"


/* Initialize the lfu list */
int lfu_init(struct lfu_ls **llist, index_t max)
{
    index_t i = 0;

    EIO_ASSERT(max > 0);
    *llist = vmalloc((sizeof(struct lfu_ls) + (max - 1) * sizeof(struct lfu_elem)));
    if (*llist == NULL)
        return -ENOMEM;

    (*llist)->ll_head = LFU_NULL;
    (*llist)->ll_tail = LFU_NULL;
    (*llist)->ll_max = max;
    (*llist)->ll_size = 0;

    for (i = 0; i < max; i++) {
        (*llist)->ll_elem[i].le_next = LFU_NULL;
        (*llist)->ll_elem[i].le_prev = LFU_NULL;
        (*llist)->ll_elem[i].le_key = 0;
    }

    return 0;
}

/* Uninitialize the lfu list */
void lfu_uninit(struct lfu_ls *llist)
{
    if (llist)
        vfree(llist);
}

/* Add a new entry to lfu list */
// todo: check this function
int lfu_add(struct lfu_ls *llist, index_t index, u_int64_t key)
{
    if (!llist || (index >= llist->ll_max))
        return -EINVAL;

    llist->ll_elem[index].le_prev = llist->ll_tail;
    llist->ll_elem[index].le_next = LFU_NULL;
    llist->ll_elem[index].le_key = key;

    if (llist->ll_tail != LFU_NULL)
        llist->ll_elem[llist->ll_tail].le_next = index;
    else {
        EIO_ASSERT(llist->ll_head == LFU_NULL);
        llist->ll_head = index;
    }
    llist->ll_tail = index;
    llist->ll_size++;

    u_int64_t counter = llist->ll_size;
    if (counter > 1) {
        /*
         * Sort lfu list using insert elements in lfu list if needed
         */
        index_t prev_index = index;
        index_t dest_index = index;
        while (counter > 1) {
            prev_index = llist->ll_elem[prev_index].le_prev;
            if (llist->ll_elem[index].le_key < llist->ll_elem[prev_index].le_key){
                dest_index = prev_index;
            } else {
                break;
            }
            counter--;
        }
        /*
         * If dest_index is not equal to index, so we must apply insert and remove
         */
        if (dest_index != index){
            llist->ll_tail = llist->ll_elem[index].le_prev;
            llist->ll_elem[llist->ll_elem[index].le_prev].le_next = LFU_NULL;
            llist->ll_elem[index].le_next = dest_index;
            llist->ll_elem[index].le_prev = llist->ll_elem[dest_index].le_prev;
            llist->ll_elem[dest_index].le_prev = index;
            if (dest_index == llist->ll_head){
                llist->ll_head = index;
            }
        }
    }

    return 0;
}

/* Remove an entry from the lfu list */
int lfu_rem(struct lfu_ls *llist, index_t index)
{
    if (!llist || (index >= llist->ll_max) || (index == LFU_NULL))
        return -EINVAL;

    if (llist->ll_head == LFU_NULL && llist->ll_tail == LFU_NULL)
        /*
         * No element in the list.
         */
        return -EINVAL;

    if (llist->ll_elem[index].le_prev == LFU_NULL &&
        llist->ll_elem[index].le_next == LFU_NULL &&
        llist->ll_head != index && llist->ll_tail != index)
        /*
         * Element not in list.
         */
        return 0;

    if (llist->ll_elem[index].le_prev != LFU_NULL)
        llist->ll_elem[llist->ll_elem[index].le_prev].le_next =
                llist->ll_elem[index].le_next;

    if (llist->ll_elem[index].le_next != LFU_NULL)
        llist->ll_elem[llist->ll_elem[index].le_next].le_prev =
                llist->ll_elem[index].le_prev;

    if (llist->ll_head == index)
        llist->ll_head = llist->ll_elem[index].le_next;

    if (llist->ll_tail == index)
        llist->ll_tail = llist->ll_elem[index].le_prev;

    llist->ll_elem[index].le_prev = LFU_NULL;
    llist->ll_elem[index].le_next = LFU_NULL;
    EIO_ASSERT(llist->ll_size != 0);
    llist->ll_size--;

    return 0;
}

/* Move up the given lfu element */
int lfu_touch(struct lfu_ls *llist, index_t index){
    if (!llist || (index >= llist->ll_max))
        return -EINVAL;

    u_int64_t key = llist->ll_elem[index].le_key;
    lfu_rem(llist, index);
    lfu_add(llist, index, key + 1);
    return 0;
}

/* Read the element at the head of the lfu */
int lfu_read_head(struct lfu_ls *llist, index_t *index, u_int64_t *key){
    if (!llist || !index || !key)
        return -EINVAL;

    *index = llist->ll_head;
    if (llist->ll_head == LFU_NULL) {
        *key = 0;
    } else {
        *key = llist->ll_elem[*index].le_key;
    }

    return 0;
}

/* Remove the element at the head of the lfu */
int lfu_rem_head(struct lfu_ls *llist, index_t *index, u_int64_t *key){
    if (!llist || !index || !key)
        return -EINVAL;

    *index = llist->ll_head;
    if (llist->ll_head == LFU_NULL) {
        *key = 0;
    } else {
        *key = llist->ll_elem[*index].le_key;
        lfu_rem(llist, *index);
    }

    return 0;
}
