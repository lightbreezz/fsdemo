#ifndef _TYPES_H_
#define _TYPES_H_

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/ioctl.h>

#define MAX_NAME_LEN    128     

/******************************************************************************
* SECTION: Type def
*******************************************************************************/
typedef int          boolean;
typedef uint16_t     flag16;

typedef enum file_type {
    NEWFS_REG_FILE,
    NEWFS_DIR,
    // NEWFS_SYM_LINK
} FILE_TYPE;

struct custom_options {
    char*        device;
};

// 前向声明
struct newfs_dentry;
struct newfs_inode;
struct newfs_super;

/******************************************************************************
* SECTION: Macro
*******************************************************************************/
#define TRUE                    1
#define FALSE                   0
#define UINT32_BITS             32
#define UINT8_BITS              8

// 偏移
#define NEWFS_SUPER_OFS           0
#define NEWFS_ROOT_INO            0

#define NEWFS_ERROR_NONE          0
#define NEWFS_ERROR_ACCESS        EACCES
#define NEWFS_ERROR_SEEK          ESPIPE     
#define NEWFS_ERROR_ISDIR         EISDIR
#define NEWFS_ERROR_NOSPACE       ENOSPC
#define NEWFS_ERROR_EXISTS        EEXIST
#define NEWFS_ERROR_NOTFOUND      ENOENT
#define NEWFS_ERROR_UNSUPPORTED   ENXIO
#define NEWFS_ERROR_IO            EIO     /* Error Input/Output */
#define NEWFS_ERROR_INVAL         EINVAL  /* Invalid Args */

#define NEWFS_MAX_FILE_NAME       128   /* 文件名最长多少个字符 */
#define NEWFS_INODE_PER_FILE      1     /* 一个文件索引多少个逻辑块 */
#define NEWFS_DATA_PER_FILE       6     /* 一个文件多少逻辑块 */
#define NEWFS_DEFAULT_PERM        0777

#define NEWFS_IOC_MAGIC           'S'
#define NEWFS_IOC_SEEK            _IO(NEWFS_IOC_MAGIC, 0)

#define NEWFS_FLAG_BUF_DIRTY      0x1
#define NEWFS_FLAG_BUF_OCCUPY     0x2

#define NEWFS_SUPER_BLOCKS        1     /* super占用多少块 */
#define NEWFS_INODE_MAP_BLOCKS    1     /* inode位图占用多少块 */
#define NEWFS_DATA_MAP_BLOCKS     1     /* 数据位图占用多少块 */
#define NEWFS_MAX_INODE           585  /* 最大支持多少个inode */
#define NEWFS_DATA_BLOCKS         3508   /* 数据区总共有多少块 */

/******************************************************************************
* SECTION: Macro Function
*******************************************************************************/
#define NEWFS_IO_SZ()                     (newfs_super.sz_io)
#define NEWFS_DISK_SZ()                   (newfs_super.sz_disk)
#define NEWFS_DRIVER()                    (newfs_super.driver_fd)

#define NEWFS_ROUND_DOWN(value, round)    ((value) % (round) == 0 ? (value) : ((value) / (round)) * (round))
#define NEWFS_ROUND_UP(value, round)      ((value) % (round) == 0 ? (value) : ((value) / (round) + 1) * (round))

#define NEWFS_BLKS_SZ(blks)               ((blks) * newfs_super.sz_blk)
#define NEWFS_ASSIGN_FNAME(pnewfs_dentry, _fname)   memcpy((pnewfs_dentry)->fname, (_fname), strlen(_fname))

#define NEWFS_INO_OFS(ino)                (newfs_super.inode_offset + (ino) * NEWFS_BLKS_SZ(1))
#define NEWFS_DATA_OFS(dno)               (newfs_super.data_offset + (dno) * NEWFS_BLKS_SZ(1))

#define NEWFS_IS_DIR(pinode)              ((pinode)->dentry->ftype == NEWFS_DIR)
#define NEWFS_IS_REG(pinode)              ((pinode)->dentry->ftype == NEWFS_REG_FILE)
// #define NEWFS_IS_SYM_LINK(pinode)      ((pinode)->dentry->ftype == NEWFS_SYM_LINK)

#define NEWFS_DENTRY_PER_BLOCK            (NEWFS_BLKS_SZ(1) / sizeof(struct newfs_dentry_d))

struct newfs_super {
    uint32_t magic;
    int      driver_fd;                   /* 文件描述符，保留 int */
    uint32_t sz_io;                       /* I/O 块大小 */
    uint32_t sz_blk;                      /* 逻辑块大小 */
    uint32_t sz_disk;                     /* 磁盘总大小（字节） */
    uint32_t sz_usage;                    /* 已用空间（字节） */

    uint32_t max_ino;                     /* 最大 inode 数 */
    uint8_t* map_inode;                   /* inode 位图内存指针 */
    uint32_t map_inode_blks;              /* inode 位图占多少块 */
    uint32_t map_inode_offset;            /* inode 位图起始偏移 */
    uint32_t inode_offset;                /* inode 区起始偏移 */

    uint32_t max_data;                    /* 最大数据块数 */
    uint8_t* map_data;                    /* 数据位图内存指针 */
    uint32_t map_data_blks;               /* 数据位图占多少块 */
    uint32_t map_data_offset;             /* 数据位图起始偏移 */
    uint32_t data_offset;                 /* 数据区起始偏移 */

    boolean  is_mounted;
    struct newfs_dentry* root_dentry;
};

struct newfs_inode {
    uint32_t ino;                         /* inode 编号 */
    uint32_t size;                        /* 文件已占用字节数 */
    uint32_t dir_cnt;                     /* 目录项数量（仅目录有效） */
    struct newfs_dentry* dentry;          /* 指向该 inode 的 dentry */
    struct newfs_dentry* dentrys;         /* 所有子目录项（仅目录有效） */
    uint32_t data_blocks[NEWFS_DATA_PER_FILE];   /* 数据块编号列表 */
    uint8_t* data_in_mem[NEWFS_DATA_PER_FILE];   /* 数据块在内存中的缓存指针 */
    uint32_t data_block_nums;             /* 实际使用的数据块数量 */
};  

struct newfs_dentry {
    char fname[NEWFS_MAX_FILE_NAME];
    uint32_t ino;
    struct newfs_dentry* parent;          /* 父目录 dentry */
    struct newfs_dentry* brother;         /* 同级兄弟 dentry */
    struct newfs_inode* inode;            /* 指向 inode */
    FILE_TYPE ftype;
};

static inline struct newfs_dentry* new_dentry(char * fname, FILE_TYPE ftype) {
    struct newfs_dentry * dentry = (struct newfs_dentry *)malloc(sizeof(struct newfs_dentry));
    memset(dentry, 0, sizeof(struct newfs_dentry));
    NEWFS_ASSIGN_FNAME(dentry, fname);
    dentry->ftype   = ftype;
    dentry->ino     = (uint32_t)-1;       /* 无效 ino，用最大值表示 */
    dentry->inode   = NULL;
    dentry->parent  = NULL;
    dentry->brother = NULL;
    return dentry;
}

/******************************************************************************
* SECTION: FS Specific Structure - Disk structure
*******************************************************************************/
struct newfs_super_d {
    uint32_t magic_num;
    uint32_t sz_usage;

    uint32_t max_ino;
    uint32_t map_inode_blks;
    uint32_t map_inode_offset;
    uint32_t inode_offset;

    uint32_t max_data;
    uint32_t map_data_blks;
    uint32_t map_data_offset;
    uint32_t data_offset;
};

struct newfs_inode_d {
    uint32_t ino;
    uint32_t size;
    uint32_t dir_cnt;
    FILE_TYPE ftype;
    uint32_t data_block_nums;
    uint32_t data_blocks[NEWFS_DATA_PER_FILE];
};  

struct newfs_dentry_d {
    char fname[NEWFS_MAX_FILE_NAME];
    FILE_TYPE ftype;
    uint32_t ino;
};  

#endif /* _TYPES_H_ */