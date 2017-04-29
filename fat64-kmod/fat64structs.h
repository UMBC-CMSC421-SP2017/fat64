#ifndef _FAT64STRUCTS_H
#define _FAT64STRUCTS_H

struct fat64_superblock {
  __u8 fs_magic[8];
  __le32 block_size;
  __le32 blocks_per_cluster;
  __le64 num_blocks;
  __le16 major_version;
  __le16 minor_version;
  __le32 flags;
  __le64 rootdir_cluster;
  __le16 fat_copies;
  __le16 backup_super;
  __le16 reserved_sectors;
  __le16 fs_state;
  __le64 blocks_per_fat;
  __le64 reserved1;
  __u8 uuid[16];
  __u8 fs_name[16];
  __le64 mtime;
  __le64 wtime;
  __le32 mount_count;
  __le32 reserved2;
  __u8 created_by[8];
  __le64 free_clusters;
  __le64 last_used_cluster;
  __le32 dir_prealloc;
  __le32 compression_algo;
  __le32 encryption_algo;
  __le32 reserved3;
  __le64 fat_enc_key;
  __le64 rootdir_enc_key;
  __u8 reserved4[48];
  __u8 hash[32];
  __u8 reserved5[768];
};

struct fat64_dir_entry {
  __le16 next_ptr;
  __u8 name_len;
  __u8 file_type;
  __le16 uid;
  __le16 gid;
  __le64 first_cluster;
  __le64 file_length;
  __le64 ctime;
  __le64 mtime;
  __le64 atime;
  __le16 mode;
  __le16 reserved;
  __u8 name[];
};

/* start of data cluster's entry (number of reserved clusters) */
#define FAT64_START_ENT	2

#define FAT64_ROOT_INO	 1	/* The root inode number */


#define BAD_FAT64	0xFFFFFFFFFFFFFFF7ULL
#define EOF_FAT64	0xFFFFFFFFFFFFFFFFULL

#define FAT_ENT_FREE	(0)
#define FAT_ENT_BAD	(BAD_FAT64)
#define FAT_ENT_EOF	(EOF_FAT64)

#define IS_FREE(n)	(!*(n))

#endif /* !_FAT64STRUCTS_H */
