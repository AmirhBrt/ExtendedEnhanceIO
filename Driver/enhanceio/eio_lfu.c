/*
 * Created by Amirhossein Barati on 2/1/23.
 *
 *  eio_lfu.c
 *
 *  Copyright (C) 2012 STEC, Inc. All rights not specifically granted
 *   under a license included herein are reserved
 *  Made EnhanceIO specific changes.
 *   Saied Kazemi <skazemi@stec-inc.com>
 *   Siddharth Choudhuri <schoudhuri@stec-inc.com>
 *
 *  Copyright 2010 Facebook, Inc.
 *   Author: Mohan Srinivasan (mohan@facebook.com)
 *
 *  Based on DM-Cache:
 *   Copyright (C) International Business Machines Corp., 2006
 *   Author: Ming Zhao (mingzhao@ufl.edu)
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
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */


#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include "eio.h"
/* Generic policy functions prototyes */
int eio_lfu_init(struct cache_c *);
void eio_lfu_exit(void);
int eio_lfu_cache_sets_init(struct eio_policy *);
int eio_lfu_cache_blk_init(struct eio_policy *);
void eio_lfu_find_reclaim_dbn(struct eio_policy *, index_t, index_t *);
int eio_lfu_clean_set(struct eio_policy *, index_t, int);
/* Per policy instance initialization */
struct eio_policy *eio_lfu_instance_init(void);

/* LFU specific policy functions prototype */
void eio_lfu_pushblks(struct eio_policy *);
void eio_reclaim_lfu_movetail(struct cache_c *, index_t, struct eio_policy *);

/* Per cache set data structure */
struct eio_lfu_cache_set {
    u_int16_t lfu_head, lfu_tail;
};

/* Per cache block data structure */
struct eio_lfu_cache_block {
    u_int16_t lfu_prev, lfu_next;
};

/* LFU specifc data structures */
static struct eio_lfu eio_lfu = {
        .sl_lfu_pushblks		= eio_lfu_pushblks,
        .sl_reclaim_lfu_movetail	= eio_reclaim_lfu_movetail,
};

/*
 * Context that captures the LFU replacement policy
 */
static struct eio_policy_header eio_lfu_ops = {
        .sph_name		= CACHE_REPL_LFU,
        .sph_instance_init	= eio_lfu_instance_init,
};

/*
 * Intialize LFU. Called from ctr.
 */
int eio_lfu_init(struct cache_c *dmc)
{
    return 0;
}

/*
 * Initialize per set LFU data structures.
 */
int eio_lfu_cache_sets_init(struct eio_policy *p_ops)
{
    sector_t order;
    int i;
    struct cache_c *dmc = p_ops->sp_dmc;
    struct eio_lfu_cache_set *cache_sets;

    order =
            (dmc->size >> dmc->consecutive_shift) *
            sizeof(struct eio_lfu_cache_set);

    dmc->sp_cache_set = vmalloc((size_t)order);
    if (dmc->sp_cache_set == NULL)
        return -ENOMEM;

    cache_sets = (struct eio_lfu_cache_set *)dmc->sp_cache_set;

    for (i = 0; i < (int)(dmc->size >> dmc->consecutive_shift); i++) {
        cache_sets[i].lfu_tail = EIO_LFU_NULL;
        cache_sets[i].lfu_head = EIO_LFU_NULL;
    }
    pr_info("Initialized %d sets in LFU", i);

    return 0;
}

/*
 * Initialize per block LFU data structures
 */
int eio_lfu_cache_blk_init(struct eio_policy *p_ops)
{
    sector_t order;
    struct cache_c *dmc = p_ops->sp_dmc;

    order = dmc->size * sizeof(struct eio_lfu_cache_block);

    dmc->sp_cache_blk = vmalloc((size_t)order);
    if (dmc->sp_cache_blk == NULL)
        return -ENOMEM;

    return 0;
}

/*
 * Allocate a new instance of eio_policy per dmc
 */
struct eio_policy *eio_lfu_instance_init(void)
{
    struct eio_policy *new_instance;

    new_instance = vmalloc(sizeof(struct eio_policy));
    if (new_instance == NULL) {
        pr_err("eio_lfu_instance_init: vmalloc failed");
        return NULL;
    }

    /* Initialize the LFU specific functions and variables */
    new_instance->sp_name = CACHE_REPL_LFU;
    new_instance->sp_policy.lfu = &eio_lfu;
    new_instance->sp_repl_init = eio_lfu_init;
    new_instance->sp_repl_exit = eio_lfu_exit;
    new_instance->sp_repl_sets_init = eio_lfu_cache_sets_init;
    new_instance->sp_repl_blk_init = eio_lfu_cache_blk_init;
    new_instance->sp_find_reclaim_dbn = eio_lfu_find_reclaim_dbn;
    new_instance->sp_clean_set = eio_lfu_clean_set;
    new_instance->sp_dmc = NULL;

    try_module_get(THIS_MODULE);

    pr_info("eio_lfu_instance_init: created new instance of LFU");

    return new_instance;
}

/*
 * Cleanup an instance of eio_policy (called from dtr).
 */
void eio_lfu_exit(void)
{
    module_put(THIS_MODULE);
}

/*
 * Find a victim block to evict and return it in index.
 */
void
eio_lfu_find_reclaim_dbn(struct eio_policy *p_ops,
                         index_t start_index, index_t *index)
{
    index_t lfu_rel_index;
    struct eio_lfu_cache_set *lfu_sets;
    struct eio_lfu_cache_block *lfu_blk;
    struct cache_c *dmc = p_ops->sp_dmc;
    index_t set;

    set = start_index / dmc->assoc;
    lfu_sets = (struct eio_lfu_cache_set *)(dmc->sp_cache_set);

    lfu_rel_index = lfu_sets[set].lfu_head;
    while (lfu_rel_index != EIO_LFU_NULL) {
        lfu_blk =
                ((struct eio_lfu_cache_block *)dmc->sp_cache_blk +
                 lfu_rel_index + start_index);
        if (EIO_CACHE_STATE_GET(dmc, (lfu_rel_index + start_index)) ==
            VALID) {
            EIO_ASSERT((lfu_blk - (struct eio_lfu_cache_block *)
                    dmc->sp_cache_blk) ==
                       (lfu_rel_index + start_index));
            *index = lfu_rel_index + start_index;
            eio_reclaim_lfu_movetail(dmc, *index, p_ops);
            break;
        }
        lfu_rel_index = lfu_blk->lfu_next;
    }

    return;
}

/*
 * Go through the entire set and clean.
 */
int eio_lfu_clean_set(struct eio_policy *p_ops, index_t set, int to_clean)
{
    struct cache_c *dmc = p_ops->sp_dmc;
    index_t lfu_rel_index;
    int nr_writes = 0;
    struct eio_lfu_cache_set *lfu_cache_sets;
    struct eio_lfu_cache_block *lfu_cacheblk;
    index_t dmc_idx;
    index_t start_index;

    lfu_cache_sets = (struct eio_lfu_cache_set *)dmc->sp_cache_set;
    start_index = set * dmc->assoc;
    lfu_rel_index = lfu_cache_sets[set].lfu_head;

    while ((lfu_rel_index != EIO_LFU_NULL) && (nr_writes < to_clean)) {
        dmc_idx = lfu_rel_index + start_index;
        lfu_cacheblk =
                ((struct eio_lfu_cache_block *)dmc->sp_cache_blk +
                 lfu_rel_index + start_index);
        EIO_ASSERT((lfu_cacheblk -
                    (struct eio_lfu_cache_block *)dmc->sp_cache_blk) ==
                   (lfu_rel_index + start_index));
        if ((EIO_CACHE_STATE_GET(dmc, dmc_idx) &
             (DIRTY | BLOCK_IO_INPROG)) == DIRTY) {
            EIO_CACHE_STATE_ON(dmc, dmc_idx, DISKWRITEINPROG);
            nr_writes++;
        }
        lfu_rel_index = lfu_cacheblk->lfu_next;
    }

    return nr_writes;
}

/*
 * LFU specific functions.
 */
void
eio_reclaim_lfu_movetail(struct cache_c *dmc, index_t index,
                         struct eio_policy *p_ops)
{
    index_t set = index / dmc->assoc;
    index_t start_index = set * dmc->assoc;
    index_t my_index = index - start_index;
    struct eio_lfu_cache_block *cacheblk;
    struct eio_lfu_cache_set *cache_sets;
    struct eio_lfu_cache_block *blkptr;

    cacheblk =
            (((struct eio_lfu_cache_block *)(dmc->sp_cache_blk)) + index);
    cache_sets = (struct eio_lfu_cache_set *)dmc->sp_cache_set;
    blkptr = (struct eio_lfu_cache_block *)(dmc->sp_cache_blk);

    /* Remove from LFU */
    if (likely((cacheblk->lfu_prev != EIO_LFU_NULL) ||
               (cacheblk->lfu_next != EIO_LFU_NULL))) {
        if (cacheblk->lfu_prev != EIO_LFU_NULL)
            blkptr[cacheblk->lfu_prev + start_index].lfu_next =
                    cacheblk->lfu_next;
        else
            cache_sets[set].lfu_head = cacheblk->lfu_next;
        if (cacheblk->lfu_next != EIO_LFU_NULL)
            blkptr[cacheblk->lfu_next + start_index].lfu_prev =
                    cacheblk->lfu_prev;
        else
            cache_sets[set].lfu_tail = cacheblk->lfu_prev;
    }
    /* And add it to LFU Tail */
    cacheblk->lfu_next = EIO_LFU_NULL;
    cacheblk->lfu_prev = cache_sets[set].lfu_tail;
    if (cache_sets[set].lfu_tail == EIO_LFU_NULL)
        cache_sets[set].lfu_head = (u_int16_t)my_index;
    else
        blkptr[cache_sets[set].lfu_tail + start_index].lfu_next =
                (u_int16_t)my_index;
    cache_sets[set].lfu_tail = (u_int16_t)my_index;
}

void eio_lfu_pushblks(struct eio_policy *p_ops)
{
    struct cache_c *dmc = p_ops->sp_dmc;
    struct eio_lfu_cache_block *cache_block;
    int i;

    cache_block = dmc->sp_cache_blk;
    for (i = 0; i < (int)dmc->size; i++) {
        cache_block[i].lfu_prev = EIO_LFU_NULL;
        cache_block[i].lfu_next = EIO_LFU_NULL;
        eio_reclaim_lfu_movetail(dmc, i, p_ops);
    }
    return;
}

static
int __init lfu_register(void)
{
    int ret;

    ret = eio_register_policy(&eio_lfu_ops);
    if (ret != 0)
        pr_info("eio_lfu already registered");

    return ret;
}

static
void __exit lfu_unregister(void)
{
    int ret;

    ret = eio_unregister_policy(&eio_lfu_ops);
    if (ret != 0)
        pr_err("eio_lfu unregister failed");
}

module_init(lfu_register);
module_exit(lfu_unregister);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("LFU policy for EnhanceIO");
MODULE_AUTHOR("STEC, Inc. based on code by Facebook");
