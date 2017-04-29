/*
 *  linux/fs/fat/cache.c
 *
 *  Written 1992,1993 by Werner Almesberger
 *
 *  Mar 1999. AV. Changed cache, so that it uses the starting cluster instead
 *	of inode number.
 *  May 1999. AV. Fixed the bogosity with FAT32 (read "FAT28"). Fscking lusers.
 */

#include <linux/slab.h>
#include "fat.h"

/* this must be > 0. */
#define FAT_MAX_CACHE	8

struct fat_cache {
	struct list_head cache_list;
	__u64 nr_contig;	/* number of contiguous clusters */
	__u64 fcluster;	/* cluster number in the file. */
	__u64 dcluster;	/* cluster number on disk. */
};

struct fat_cache_id {
	unsigned int id;
	__u64 nr_contig;
	__u64 fcluster;
	__u64 dcluster;
};

static inline int fat_max_cache(struct inode *inode)
{
	return FAT_MAX_CACHE;
}

static struct kmem_cache *fat_cache_cachep;

static void init_once(void *foo)
{
	struct fat_cache *cache = (struct fat_cache *)foo;

	INIT_LIST_HEAD(&cache->cache_list);
}

int __init fat64_cache_init(void)
{
	fat_cache_cachep = kmem_cache_create("fat_cache",
				sizeof(struct fat_cache),
				0, SLAB_RECLAIM_ACCOUNT|SLAB_MEM_SPREAD,
				init_once);
	if (fat_cache_cachep == NULL)
		return -ENOMEM;
	return 0;
}

void fat64_cache_destroy(void)
{
	kmem_cache_destroy(fat_cache_cachep);
}

static inline struct fat_cache *fat_cache_alloc(struct inode *inode)
{
	return kmem_cache_alloc(fat_cache_cachep, GFP_NOFS);
}

static inline void fat_cache_free(struct fat_cache *cache)
{
	BUG_ON(!list_empty(&cache->cache_list));
	kmem_cache_free(fat_cache_cachep, cache);
}

static inline void fat_cache_update_lru(struct inode *inode,
					struct fat_cache *cache)
{
	if (MSDOS_I(inode)->cache_lru.next != &cache->cache_list)
		list_move(&cache->cache_list, &MSDOS_I(inode)->cache_lru);
}

static int fat_cache_lookup(struct inode *inode, __u64 fclus,
			    struct fat_cache_id *cid,
			    __u64 *cached_fclus, __u64 *cached_dclus)
{
	static struct fat_cache nohit = { .fcluster = 0, };

	struct fat_cache *hit = &nohit, *p;
	int offset = -1;

	spin_lock(&MSDOS_I(inode)->cache_lru_lock);
	list_for_each_entry(p, &MSDOS_I(inode)->cache_lru, cache_list) {
		/* Find the cache of "fclus" or nearest cache. */
		if (p->fcluster <= fclus && hit->fcluster < p->fcluster) {
			hit = p;
			if ((hit->fcluster + hit->nr_contig) < fclus) {
				offset = hit->nr_contig;
			} else {
				offset = fclus - hit->fcluster;
				break;
			}
		}
	}
	if (hit != &nohit) {
		fat_cache_update_lru(inode, hit);

		cid->id = MSDOS_I(inode)->cache_valid_id;
		cid->nr_contig = hit->nr_contig;
		cid->fcluster = hit->fcluster;
		cid->dcluster = hit->dcluster;
		*cached_fclus = cid->fcluster + offset;
		*cached_dclus = cid->dcluster + offset;
	}
	spin_unlock(&MSDOS_I(inode)->cache_lru_lock);

	return offset;
}

static struct fat_cache *fat_cache_merge(struct inode *inode,
					 struct fat_cache_id *new)
{
	struct fat_cache *p;

	list_for_each_entry(p, &MSDOS_I(inode)->cache_lru, cache_list) {
		/* Find the same part as "new" in cluster-chain. */
		if (p->fcluster == new->fcluster) {
			BUG_ON(p->dcluster != new->dcluster);
			if (new->nr_contig > p->nr_contig)
				p->nr_contig = new->nr_contig;
			return p;
		}
	}
	return NULL;
}

static void fat_cache_add(struct inode *inode, struct fat_cache_id *new)
{
	struct fat_cache *cache, *tmp;

	if (new->fcluster == -1) /* dummy cache */
		return;

	spin_lock(&MSDOS_I(inode)->cache_lru_lock);
	if (new->id != FAT_CACHE_VALID &&
	    new->id != MSDOS_I(inode)->cache_valid_id)
		goto out;	/* this cache was invalidated */

	cache = fat_cache_merge(inode, new);
	if (cache == NULL) {
		if (MSDOS_I(inode)->nr_caches < fat_max_cache(inode)) {
			MSDOS_I(inode)->nr_caches++;
			spin_unlock(&MSDOS_I(inode)->cache_lru_lock);

			tmp = fat_cache_alloc(inode);
			if (!tmp) {
				spin_lock(&MSDOS_I(inode)->cache_lru_lock);
				MSDOS_I(inode)->nr_caches--;
				spin_unlock(&MSDOS_I(inode)->cache_lru_lock);
				return;
			}

			spin_lock(&MSDOS_I(inode)->cache_lru_lock);
			cache = fat_cache_merge(inode, new);
			if (cache != NULL) {
				MSDOS_I(inode)->nr_caches--;
				fat_cache_free(tmp);
				goto out_update_lru;
			}
			cache = tmp;
		} else {
			struct list_head *p = MSDOS_I(inode)->cache_lru.prev;
			cache = list_entry(p, struct fat_cache, cache_list);
		}
		cache->fcluster = new->fcluster;
		cache->dcluster = new->dcluster;
		cache->nr_contig = new->nr_contig;
	}
out_update_lru:
	fat_cache_update_lru(inode, cache);
out:
	spin_unlock(&MSDOS_I(inode)->cache_lru_lock);
}

/*
 * Cache invalidation occurs rarely, thus the LRU chain is not updated. It
 * fixes itself after a while.
 */
static void __fat64_cache_inval_inode(struct inode *inode)
{
	struct msdos_inode_info *i = MSDOS_I(inode);
	struct fat_cache *cache;

	while (!list_empty(&i->cache_lru)) {
		cache = list_entry(i->cache_lru.next,
				   struct fat_cache, cache_list);
		list_del_init(&cache->cache_list);
		i->nr_caches--;
		fat_cache_free(cache);
	}
	/* Update. The copy of caches before this id is discarded. */
	i->cache_valid_id++;
	if (i->cache_valid_id == FAT_CACHE_VALID)
		i->cache_valid_id++;
}

void fat64_cache_inval_inode(struct inode *inode)
{
	spin_lock(&MSDOS_I(inode)->cache_lru_lock);
	__fat64_cache_inval_inode(inode);
	spin_unlock(&MSDOS_I(inode)->cache_lru_lock);
}

static inline int cache_contiguous(struct fat_cache_id *cid, int dclus)
{
	cid->nr_contig++;
	return ((cid->dcluster + cid->nr_contig) == dclus);
}

static inline void cache_init(struct fat_cache_id *cid, int fclus, int dclus)
{
	cid->id = FAT_CACHE_VALID;
	cid->fcluster = fclus;
	cid->dcluster = dclus;
	cid->nr_contig = 0;
}

int fat64_get_cluster(struct inode *inode, __u64 cluster, __u64 *fclus, __u64 *dclus)
{
	struct super_block *sb = inode->i_sb;
	const int limit = sb->s_maxbytes >> MSDOS_SB(sb)->cluster_bits;
	struct fat_entry fatent;
	struct fat_cache_id cid;
	__u64 nr;
	int rv;

	BUG_ON(MSDOS_I(inode)->i_start == 0);

	*fclus = 0;
	*dclus = MSDOS_I(inode)->i_start;
	if (cluster == 0)
		return 0;

	if (fat_cache_lookup(inode, cluster, &cid, fclus, dclus) < 0) {
		/*
		 * dummy, always not contiguous
		 * This is reinitialized by cache_init(), later.
		 */
		cache_init(&cid, -1, -1);
	}

	fatent_init(&fatent);
	while (*fclus < cluster) {
		/* prevent the infinite loop of cluster chain */
		if (*fclus > limit) {
			fat64_fs_error_ratelimit(sb,
					"%s: detected the cluster chain loop"
					" (i_pos %lld)", __func__,
					MSDOS_I(inode)->i_pos);
			rv = -EIO;
			goto out;
		}

#warning XXXX
		nr = fat64_ent_read(inode, &fatent, *dclus);
		if (nr < 0) {
			rv = (int)nr;
			goto out;
		}
		else if (nr == FAT_ENT_FREE) {
			fat64_fs_error_ratelimit(sb,
				       "%s: invalid cluster chain (i_pos %lld)",
				       __func__,
				       MSDOS_I(inode)->i_pos);
			rv = -EIO;
			goto out;
		} else if (nr == FAT_ENT_EOF) {
			fat_cache_add(inode, &cid);
			#warning ????
			rv = (int)FAT_ENT_EOF;
			goto out;
		}
		(*fclus)++;
		*dclus = nr;
		if (!cache_contiguous(&cid, *dclus))
			cache_init(&cid, *fclus, *dclus);
	}
	rv = 0;
	fat_cache_add(inode, &cid);
out:
	fatent_brelse(&fatent);
	return rv;
}

static __u64 fat64_bmap_cluster(struct inode *inode, __u64 cluster)
{
	struct super_block *sb = inode->i_sb;
	int ret;
	__u64 fclus, dclus;

	if (MSDOS_I(inode)->i_start == 0)
		return 0;

	ret = fat64_get_cluster(inode, cluster, &fclus, &dclus);
	if (ret < 0)
		return (__u64)ret;
	else if (ret == FAT_ENT_EOF) {
		fat64_fs_error(sb, "%s: request beyond EOF (i_pos %lld)",
			     __func__, MSDOS_I(inode)->i_pos);
		return (__u64)-EIO;
	}
	return dclus;
}

int fat64_bmap(struct inode *inode, sector_t sector, sector_t *phys,
	     unsigned long *mapped_blocks, int create)
{
	struct super_block *sb = inode->i_sb;
	struct msdos_sb_info *sbi = MSDOS_SB(sb);
	const unsigned long blocksize = sb->s_blocksize;
	const unsigned char blocksize_bits = sb->s_blocksize_bits;
	sector_t last_block;
	int cluster, offset;

	*phys = 0;
	*mapped_blocks = 0;
	if ((sbi->fat_bits != 32) && (inode->i_ino == FAT64_ROOT_INO)) {
		if (sector < (sbi->dir_entries >> sbi->dir_per_block_bits)) {
			*phys = sector + sbi->dir_start;
			*mapped_blocks = 1;
		}
		return 0;
	}

	last_block = (i_size_read(inode) + (blocksize - 1)) >> blocksize_bits;
	if (sector >= last_block) {
		if (!create)
			return 0;

		/*
		 * ->mmu_private can access on only allocation path.
		 * (caller must hold ->i_mutex)
		 */
		last_block = (MSDOS_I(inode)->mmu_private + (blocksize - 1))
			>> blocksize_bits;
		if (sector >= last_block)
			return 0;
	}

	cluster = sector >> (sbi->cluster_bits - sb->s_blocksize_bits);
	offset  = sector & (sbi->sec_per_clus - 1);
	cluster = fat64_bmap_cluster(inode, cluster);
	if (cluster < 0)
		return cluster;
	else if (cluster) {
		*phys = fat_clus_to_blknr(sbi, cluster) + offset;
		*mapped_blocks = sbi->sec_per_clus - offset;
		if (*mapped_blocks > last_block - sector)
			*mapped_blocks = last_block - sector;
	}
	return 0;
}
