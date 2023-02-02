/*
 * Created by AmirHBrt on 2/2/23.
 *
 *  eio_setlfu.h
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

#ifndef _EIO_SETLFU_H
#define _EIO_SETLFU_H

#define         LFU_NULL        -1

struct lfu_elem {
    index_t le_next;
    index_t le_prev;
    u_int64_t le_key;
};

struct lfu_ls {
    index_t ll_head;
    index_t ll_tail;
    index_t ll_max;
    u_int64_t ll_size;
    struct lfu_elem ll_elem[1];
};

int lfu_init(struct lfu_ls **llist, index_t max);
void lfu_uninit(struct lfu_ls *llist);
int lfu_add(struct lfu_ls *llist, index_t index, u_int64_t key);
int lfu_rem(struct lfu_ls *llist, index_t index);
int lfu_touch(struct lfu_ls *llist, index_t index);
int lfu_read_head(struct lfu_ls *llist, index_t *index, u_int64_t *key);
int lfu_rem_head(struct lfu_ls *llist, index_t *index, u_int64_t *key);

#endif //_EIO_SETLFU_H
