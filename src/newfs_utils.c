#include "../include/newfs.h"

extern struct newfs_super      newfs_super; 
extern struct custom_options newfs_options;

/**
 * @brief 挂载sfs, Layout 如下
 * 
 * Layout
 * | Super | Inode Map | Data |
 * 
 * IO_SZ = BLK_SZ
 * 
 * 每个Inode占用一个Blk
 * @param options 
 * @return int 
 */
int newfs_mount(struct custom_options options){
    // 多了数据位图和逻辑位图
    int                 ret = NEWFS_ERROR_NONE;
    int                 driver_fd;
    struct newfs_super_d  newfs_super_d; 
    struct newfs_dentry*  root_dentry;
    struct newfs_inode*   root_inode;

    int                 inode_num;
    int                 map_inode_blks;

    int                data_num;
    int                map_data_blks;
    
    int                 super_blks;
    boolean             is_init = FALSE;

    newfs_super.is_mounted = FALSE;

    // driver_fd = open(options.device, O_RDWR);
    driver_fd = ddriver_open(options.device);

    if (driver_fd < 0) {
        return driver_fd;
    }

    newfs_super.driver_fd = driver_fd;
    ddriver_ioctl(NEWFS_DRIVER(), IOC_REQ_DEVICE_SIZE,  &newfs_super.sz_disk);
    ddriver_ioctl(NEWFS_DRIVER(), IOC_REQ_DEVICE_IO_SZ, &newfs_super.sz_io);
    newfs_super.sz_blk = newfs_super.sz_io * 2;
    printf("Block size: %d\n", newfs_super.sz_blk);
    
    root_dentry = new_dentry("/", NEWFS_DIR);     /* 根目录项每次挂载时新建 */

    if (newfs_driver_read(NEWFS_SUPER_OFS, (uint8_t *)(&newfs_super_d), 
                        sizeof(struct newfs_super_d)) != NEWFS_ERROR_NONE) {
        return -NEWFS_ERROR_IO;
    }   
                                                      /* 读取super */
    if (newfs_super_d.magic_num != NEWFS_MAGIC) {
        printf("init\n");
        /* 填充 in-mem 与 on-disk super */
        newfs_super.max_ino  = NEWFS_MAX_INODE;
        newfs_super.max_data = NEWFS_DATA_BLOCKS;

        newfs_super_d.magic_num        = NEWFS_MAGIC;
        newfs_super_d.sz_usage         = 0;

        newfs_super_d.max_ino          = NEWFS_MAX_INODE;
        newfs_super_d.map_inode_blks   = NEWFS_INODE_MAP_BLOCKS;
        newfs_super_d.map_inode_offset = NEWFS_SUPER_OFS + NEWFS_BLKS_SZ(NEWFS_SUPER_BLOCKS);
        printf("inode map offest %d\n", newfs_super_d.map_inode_offset);
        
        newfs_super_d.max_data         = NEWFS_DATA_BLOCKS;
        newfs_super_d.map_data_blks    = NEWFS_DATA_MAP_BLOCKS;
        newfs_super_d.map_data_offset  = newfs_super_d.map_inode_offset + NEWFS_BLKS_SZ(NEWFS_INODE_MAP_BLOCKS);
        printf("data map offest %d\n", newfs_super_d.map_data_offset);
        
        newfs_super_d.inode_offset     = newfs_super_d.map_data_offset + NEWFS_BLKS_SZ(NEWFS_DATA_MAP_BLOCKS);
        printf("inode_offest %d\n", newfs_super_d.inode_offset);
        newfs_super_d.data_offset      = newfs_super_d.inode_offset + NEWFS_BLKS_SZ(newfs_super_d.max_ino);
        printf("data_offest %d\n", newfs_super_d.data_offset);
        is_init = TRUE;
    }
    
    /* 建立内存结构（指针和偏移） */
    newfs_super.sz_usage        = newfs_super_d.sz_usage;

    newfs_super.map_inode       = (uint8_t *)malloc(NEWFS_BLKS_SZ(newfs_super_d.map_inode_blks));
    newfs_super.map_inode_blks  = newfs_super_d.map_inode_blks;
    newfs_super.map_inode_offset= newfs_super_d.map_inode_offset;
    newfs_super.inode_offset    = newfs_super_d.inode_offset;
    newfs_super.max_ino         = newfs_super_d.max_ino;

    newfs_super.map_data        = (uint8_t *)malloc(NEWFS_BLKS_SZ(newfs_super_d.map_data_blks));
    newfs_super.map_data_blks   = newfs_super_d.map_data_blks;
    newfs_super.map_data_offset = newfs_super_d.map_data_offset;
    newfs_super.data_offset     = newfs_super_d.data_offset;
    newfs_super.max_data        = newfs_super_d.max_data;

    // newfs_dump_map(); 

	printf("\n--------------------------------------------------------------------------------\n\n");

    if (newfs_driver_read(newfs_super_d.map_inode_offset, (uint8_t *)(newfs_super.map_inode), 
                        NEWFS_BLKS_SZ(newfs_super_d.map_inode_blks)) != NEWFS_ERROR_NONE) {
        return -NEWFS_ERROR_IO;
    }

    if (newfs_driver_read(newfs_super_d.map_data_offset, (uint8_t *)(newfs_super.map_data), 
                        NEWFS_BLKS_SZ(newfs_super_d.map_data_blks)) != NEWFS_ERROR_NONE) {
        return -NEWFS_ERROR_IO;
    }

    if (is_init) {                                    /* 分配根节点 */
        printf("init: alloc new root inode\n");
        root_inode = newfs_alloc_inode(root_dentry);
        newfs_sync_inode(root_inode);
    }

    root_inode            = newfs_read_inode(root_dentry, NEWFS_ROOT_INO);  /* 读取根目录 */
    root_dentry->inode    = root_inode;
    newfs_super.root_dentry = root_dentry;
    newfs_super.is_mounted  = TRUE;

    // newfs_dump_map();
    return ret;
}

/**
 * @brief 分配一个inode，占用位图
 * 
 * @param dentry 该dentry指向分配的inode
 * @return newfs_inode
 */
struct newfs_inode* newfs_alloc_inode(struct newfs_dentry * dentry) {
    struct newfs_inode* inode;
    int byte_cursor = 0; 
    int bit_cursor  = 0; 
    int ino_cursor  = 0;
    boolean is_find_free_entry = FALSE;
    /* 检查位图是否有空位 */
    for (byte_cursor = 0; byte_cursor < NEWFS_BLKS_SZ(newfs_super.map_inode_blks); 
         byte_cursor++)
    {
        for (bit_cursor = 0; bit_cursor < UINT8_BITS; bit_cursor++) {
            if((newfs_super.map_inode[byte_cursor] & (0x1 << bit_cursor)) == 0) {    
                                                      /* 当前ino_cursor位置空闲 */
                newfs_super.map_inode[byte_cursor] |= (0x1 << bit_cursor);
                is_find_free_entry = TRUE; 
                printf("Allocating inode at byte %d, bit %d\n", byte_cursor, bit_cursor);          
                break;
            }
            ino_cursor++;
        }
        if (is_find_free_entry) {
            break;
        }
    }

    if (!is_find_free_entry || ino_cursor >= newfs_super.max_ino)
        return -NEWFS_ERROR_NOSPACE;

    inode = (struct newfs_inode*)malloc(sizeof(struct newfs_inode));
    memset(inode, 0, sizeof(struct newfs_inode));
    inode->ino  = ino_cursor; 
    inode->size = 0;
    // for (int i = 0; i < NEWFS_DATA_PER_FILE; i++) {
    //     inode->data_blocks[i] = -1;
    //     inode->data_in_mem[i] = NULL;
    // }
    inode->data_block_nums = 0;
                                                      /* dentry指向inode */
    dentry->inode = inode;
    dentry->ino   = inode->ino;
                                                      /* inode指回dentry */
    inode->dentry = dentry;
    
    inode->dir_cnt = 0;
    inode->dentrys = NULL;

    if (NEWFS_IS_REG(inode)) {
        for (int i = 0; i < NEWFS_DATA_PER_FILE; i++){
            inode->data_in_mem[i] = (uint8_t *)malloc(NEWFS_BLKS_SZ(1));
            inode->data_blocks[i] = -1;
        }
    }
    printf("alloc node: alloc mem for inode %d\n", inode->ino);
    
    return inode;
}

/**
 * @brief 将内存inode及其下方结构全部刷回磁盘
 * 
 * @param inode 
 * @return int 
 */
int newfs_sync_inode(struct newfs_inode * inode) {
    struct newfs_inode_d  inode_d;
    struct newfs_dentry*  dentry_cursor;
    struct newfs_dentry_d dentry_d;

    int ino             = inode->ino;
    inode_d.ino         = ino;
    inode_d.size        = inode->size;
    // memcpy(inode_d.target_path, inode->target_path, SFS_MAX_FILE_NAME);
    inode_d.ftype       = inode->dentry->ftype;
    inode_d.dir_cnt     = inode->dir_cnt;
    inode_d.data_block_nums = inode->data_block_nums;
    printf("Syncing inode %d with %d data blocks\n", ino, inode->data_block_nums);
    for (int i = 0; i < inode->data_block_nums; i++) {
        inode_d.data_blocks[i] = inode->data_blocks[i];
    }

    int offset;
    /* 先写inode本身 */
    if (newfs_driver_write(NEWFS_INO_OFS(ino), (uint8_t *)&inode_d, 
                     sizeof(struct newfs_inode_d)) != NEWFS_ERROR_NONE) {
        NEWFS_DBG("[%s] io error\n", __func__);
        return -NEWFS_ERROR_IO;
    }

    /* 再写inode下方的数据 */
    if (NEWFS_IS_DIR(inode)) { /* 如果当前inode是目录，那么数据是目录项，且目录项的inode也要写回 */   
        int data_block_i = 0;                       
        dentry_cursor = inode->dentrys;

        while (dentry_cursor != NULL && data_block_i < inode->data_block_nums){
            offset        = NEWFS_DATA_OFS(inode->data_blocks[data_block_i]);
            // while (dentry_cursor != NULL)
            // 确保 offset + sizeof(dentry) 恰好能完全放进当前 data block
            while (dentry_cursor != NULL && \
                (offset + sizeof(struct newfs_dentry_d)) <= (NEWFS_DATA_OFS(inode->data_blocks[data_block_i]) + NEWFS_BLKS_SZ(1)))
            {
                memcpy(dentry_d.fname, dentry_cursor->fname, NEWFS_MAX_FILE_NAME);
                dentry_d.ftype = dentry_cursor->ftype;
                dentry_d.ino = dentry_cursor->ino;
                if (newfs_driver_write(offset, (uint8_t *)&dentry_d, 
                                    sizeof(struct newfs_dentry_d)) != NEWFS_ERROR_NONE) {
                    NEWFS_DBG("[%s] io error\n", __func__);
                    return -NEWFS_ERROR_IO;                     
                }
                // 递归写回子inode
                if (dentry_cursor->inode != NULL) {
                    newfs_sync_inode(dentry_cursor->inode);
                }

                dentry_cursor = dentry_cursor->brother;
                offset += sizeof(struct newfs_dentry_d);
            }
            data_block_i++;
        }
    }
    else if (NEWFS_IS_REG(inode)) { /* 如果当前inode是文件，那么数据是文件内容，直接写即可 */
        for (int i = 0; i < inode->data_block_nums; i++) {
            if (newfs_driver_write(NEWFS_DATA_OFS(inode->data_blocks[i]), 
                                 inode->data_in_mem[i], 
                                 NEWFS_BLKS_SZ(1)) != NEWFS_ERROR_NONE) {
                NEWFS_DBG("[%s] io error\n", __func__);
                return -NEWFS_ERROR_IO;
            }
        }
    }
    return NEWFS_ERROR_NONE;
}

/**
 * @brief 
 * 
 * @param dentry dentry指向ino，读取该inode
 * @param ino inode唯一编号
 * @return struct newfs_inode* 
 */
struct newfs_inode* newfs_read_inode(struct newfs_dentry * dentry, int ino) {
    struct newfs_inode* inode = (struct newfs_inode*)malloc(sizeof(struct newfs_inode));
    struct newfs_inode_d inode_d;
    struct newfs_dentry* sub_dentry;
    struct newfs_dentry_d dentry_d;
    int    dir_cnt = 0, i = 0, data_block_i = 0;
    /* 从磁盘读索引结点 */
    if (newfs_driver_read(NEWFS_INO_OFS(ino), (uint8_t *)&inode_d, 
                        sizeof(struct newfs_inode_d)) != NEWFS_ERROR_NONE) {
        NEWFS_DBG("[%s] io error\n", __func__);
        return NULL;                    
    }
    inode->dir_cnt = inode_d.dir_cnt;
    inode->ino = inode_d.ino;
    inode->size = inode_d.size;
    // memcpy(inode->target_path, inode_d.target_path, SFS_MAX_FILE_NAME);
    inode->dentry = dentry;
    inode->dentrys = NULL;
    inode->data_block_nums = inode_d.data_block_nums;
    for (int j = 0; j < inode->data_block_nums; j++) {
        inode->data_blocks[j] = inode_d.data_blocks[j];
    }
    /* 内存中的inode的数据或子目录项部分也需要读出 */
    // difficulty: 这里需要考虑目录项可能跨越多个数据块的情况
    if (NEWFS_IS_DIR(inode)) {
        dir_cnt = inode_d.dir_cnt;
        printf("dir_cnt: %d\n", dir_cnt);
        while (data_block_i < inode->data_block_nums && i < dir_cnt) {
            int offset = NEWFS_DATA_OFS(inode->data_blocks[data_block_i]);
            while (i < dir_cnt && \
                offset + sizeof(struct newfs_dentry_d) <= (NEWFS_DATA_OFS(inode->data_blocks[data_block_i]) + NEWFS_BLKS_SZ(1))) {
                if (newfs_driver_read(offset, (uint8_t *)&dentry_d, sizeof(struct newfs_dentry_d)) != NEWFS_ERROR_NONE){
                    NEWFS_DBG("[%s] io error\n", __func__);
                    return NULL;  
                }
                sub_dentry = new_dentry(dentry_d.fname, dentry_d.ftype);
                sub_dentry->parent = inode->dentry;
                sub_dentry->ino    = dentry_d.ino; 
                newfs_alloc_dentry(inode, sub_dentry);

                offset += sizeof(struct newfs_dentry_d);
                i++;
            }
            data_block_i++;
        }
    }
    else if (NEWFS_IS_REG(inode)) {
        for (int i = 0; i < inode->data_block_nums; i++) {
            if (newfs_driver_read(NEWFS_DATA_OFS(inode->data_blocks[i]), 
                                (uint8_t *)inode->data_in_mem[i], 
                                NEWFS_BLKS_SZ(1)) != NEWFS_ERROR_NONE) {
                NEWFS_DBG("[%s] io error\n", __func__);
                return NULL;                    
            }
        }
    }
    return inode;
}
/**
 * @brief 将denry插入到inode中，采用头插法
 * 
 * @param inode 
 * @param dentry 
 * @return int 
 */
int newfs_alloc_dentry(struct newfs_inode* inode, struct newfs_dentry* dentry) {
    // 修改父目录inode中的指针指向新增的dentry结构
    if (inode->dentrys == NULL) {
        inode->dentrys = dentry;
    }
    else {
        dentry->brother = inode->dentrys;
        inode->dentrys = dentry;
    }
    inode->dir_cnt++;
    inode->size += sizeof(struct newfs_dentry_d);

    if (inode->dir_cnt % NEWFS_DENTRY_PER_BLOCK == 1) {
        // 需要新增一个数据块放置 dentry
        inode->data_blocks[inode->data_block_nums] = newfs_alloc_one_block();
        inode->data_block_nums++;
    }

    return inode->dir_cnt;
}

/**
 * @brief 额外分配一个数据块
 * @return 分配的数据块号
 */
 int newfs_alloc_one_block() {
    int byte_cursor       = 0; 
    int bit_cursor        = 0;   
    int dno_cursor        = 0; 
    boolean is_find_free_data = FALSE;
    /* 检查位图是否有空位 */
    for (byte_cursor = 0; byte_cursor < NEWFS_BLKS_SZ(newfs_super.map_data_blks); byte_cursor++) {
        for (bit_cursor = 0; bit_cursor < UINT8_BITS; bit_cursor++) {
            if((newfs_super.map_data[byte_cursor] & (0x1 << bit_cursor)) == 0) {
                // 当前dno_cursor位置空闲
                newfs_super.map_data[byte_cursor] |= (0x1 << bit_cursor);
                is_find_free_data = TRUE;
                break;
            }
            dno_cursor++;
        }
        if (is_find_free_data) {
            break;
        }
    }

    if (!is_find_free_data || dno_cursor == newfs_super.max_data) {
        return -NEWFS_ERROR_NOSPACE;
    }

    return dno_cursor;
 }

 /**
 * @brief 
 * 
 * @return int 
 */
int newfs_umount() {
    struct newfs_super_d  newfs_super_d; 

    if (!newfs_super.is_mounted) {
        return NEWFS_ERROR_NONE;
    }

    newfs_sync_inode(newfs_super.root_dentry->inode);     /* 从根节点向下刷写节点 */
                                                    
    newfs_super_d.magic_num        = NEWFS_MAGIC;
    newfs_super_d.sz_usage         = newfs_super.sz_usage;

    newfs_super_d.max_ino          = newfs_super.max_ino;
    newfs_super_d.map_inode_blks   = newfs_super.map_inode_blks;
    newfs_super_d.map_inode_offset = newfs_super.map_inode_offset;
    newfs_super_d.inode_offset     = newfs_super.inode_offset;

    newfs_super_d.max_data         = newfs_super.max_data;
    newfs_super_d.map_data_blks    = newfs_super.map_data_blks;
    newfs_super_d.map_data_offset  = newfs_super.map_data_offset;
    newfs_super_d.data_offset      = newfs_super.data_offset;

    if (newfs_driver_write(NEWFS_SUPER_OFS, (uint8_t *)&newfs_super_d, 
                     sizeof(struct newfs_super_d)) != NEWFS_ERROR_NONE) {
        return -NEWFS_ERROR_IO;
    }

    if (newfs_driver_write(newfs_super_d.map_inode_offset, (uint8_t *)(newfs_super.map_inode), 
                         NEWFS_BLKS_SZ(newfs_super_d.map_inode_blks)) != NEWFS_ERROR_NONE) {
        return -NEWFS_ERROR_IO;
    }

    if (newfs_driver_write(newfs_super_d.map_data_offset, (uint8_t *)(newfs_super.map_data), 
                         NEWFS_BLKS_SZ(newfs_super_d.map_data_blks)) != NEWFS_ERROR_NONE) {
        return -NEWFS_ERROR_IO;
    }

    free(newfs_super.map_inode);
    free(newfs_super.map_data);
    ddriver_close(NEWFS_DRIVER()); 
    return NEWFS_ERROR_NONE;
}


/**
 * @brief 查找文件或目录
 * path: /qwe/ad  total_lvl = 2,
 *      1) find /'s inode       lvl = 1
 *      2) find qwe's dentry 
 *      3) find qwe's inode     lvl = 2
 *      4) find ad's dentry
 *
 * path: /qwe     total_lvl = 1,
 *      1) find /'s inode       lvl = 1
 *      2) find qwe's dentry
 *  
 * 
 * 如果能查找到，返回该目录项
 * 如果查找不到，返回的是上一个有效的路径
 * 
 * path: /a/b/c
 *      1) find /'s inode     lvl = 1
 *      2) find a's dentry 
 *      3) find a's inode     lvl = 2
 *      4) find b's dentry    如果此时找不到了，is_find=FALSE且返回的是a的inode对应的dentry
 * 
 * @param path 
 * @return struct newfs_dentry* 
 */
struct newfs_dentry* newfs_lookup(const char * path, boolean* is_find, boolean* is_root) {
    struct newfs_dentry* dentry_cursor = newfs_super.root_dentry;
    struct newfs_dentry* dentry_ret = NULL;
    struct newfs_inode*  inode; 
    int   total_lvl = newfs_calc_lvl(path);
    int   lvl = 0;
    boolean is_hit;
    char* fname = NULL;
    char* path_cpy = (char*)malloc(sizeof(path));
    *is_root = FALSE;
    strcpy(path_cpy, path);

    if (total_lvl == 0) {                           /* 根目录 */
        *is_find = TRUE;
        *is_root = TRUE;
        dentry_ret = newfs_super.root_dentry;
    }
    fname = strtok(path_cpy, "/");       
    while (fname)
    {   
        lvl++;
        if (dentry_cursor->inode == NULL) {           /* Cache机制 */
            dentry_cursor->inode = newfs_read_inode(dentry_cursor, dentry_cursor->ino);
        }

        inode = dentry_cursor->inode;

        if (NEWFS_IS_REG(inode) && lvl < total_lvl) {
            NEWFS_DBG("[%s] not a dir\n", __func__);
            dentry_ret = inode->dentry;
            break;
        }
        if (NEWFS_IS_DIR(inode)) {
            dentry_cursor = inode->dentrys;
            is_hit        = FALSE;

            while (dentry_cursor)   /* 遍历子目录项 */
            {
                if (strcmp(fname, dentry_cursor->fname) == 0) {
                    is_hit = TRUE;
                    break;
                }
                dentry_cursor = dentry_cursor->brother;
            }
            
            if (!is_hit) {
                *is_find = FALSE;
                NEWFS_DBG("[%s] not found %s\n", __func__, fname);
                dentry_ret = inode->dentry;
                break;
            }

            if (is_hit && lvl == total_lvl) {
                *is_find = TRUE;
                dentry_ret = dentry_cursor;
                break;
            }
        }
        fname = strtok(NULL, "/"); 
    }

    if (dentry_ret->inode == NULL) {
        dentry_ret->inode = newfs_read_inode(dentry_ret, dentry_ret->ino);
    }
    
    return dentry_ret;
}

/**
 * @brief 计算路径的层级
 * exm: /av/c/d/f
 * -> lvl = 4
 * @param path 
 * @return int 
 */
int newfs_calc_lvl(const char * path) {
    // char* path_cpy = (char *)malloc(strlen(path));
    // strcpy(path_cpy, path);
    char* str = path;
    int   lvl = 0;
    if (strcmp(path, "/") == 0) {
        return lvl;
    }
    while (*str != NULL) {
        if (*str == '/') {
            lvl++;
        }
        str++;
    }
    return lvl;
}

/**
 * @brief 获取文件名
 * 
 * @param path 
 * @return char* 
 */
char* newfs_get_fname(const char* path) {
    char ch = '/';
    char *q = strrchr(path, ch) + 1;
    return q;
}

/**
 * @brief 驱动读
 * 
 * @param offset 
 * @param out_content 
 * @param size 
 * @return int 
 */
int newfs_driver_read(int offset, uint8_t *out_content, int size) {
    // 1. 根据传入的offset和size，确定要读取的数据段在磁盘上的对齐位置和大小
    int      offset_aligned = NEWFS_ROUND_DOWN(offset, NEWFS_BLKS_SZ(1));
    int      bias           = offset - offset_aligned;
    int      size_aligned   = NEWFS_ROUND_UP((size + bias), NEWFS_BLKS_SZ(1));
    uint8_t* temp_content   = (uint8_t*)malloc(size_aligned);
    uint8_t* cur            = temp_content;
    // 2. 从对齐位置开始，循环读取对齐大小的数据到临时缓冲区
    // lseek(SFS_DRIVER(), offset_aligned, SEEK_SET);
    ddriver_seek(NEWFS_DRIVER(), offset_aligned, SEEK_SET);
    while (size_aligned != 0)
    {
        // read(SFS_DRIVER(), cur, SFS_IO_SZ());
        ddriver_read(NEWFS_DRIVER(), cur, NEWFS_IO_SZ());
        cur          += NEWFS_IO_SZ();
        size_aligned -= NEWFS_IO_SZ();   
    }
    // 3. 将临时缓冲区中的数据根据偏移量复制到输出缓冲区
    memcpy(out_content, temp_content + bias, size);
    free(temp_content);
    return NEWFS_ERROR_NONE;
}
/**
 * @brief 驱动写
 * 
 * @param offset 
 * @param in_content 
 * @param size 
 * @return int 
 */
int newfs_driver_write(int offset, uint8_t *in_content, int size) {
    // 1. 根据传入的offset和size，确定要写入的数据段在磁盘上的对齐位置和大小
    int      offset_aligned = NEWFS_ROUND_DOWN(offset, NEWFS_BLKS_SZ(1));
    int      bias           = offset - offset_aligned;
    int      size_aligned   = NEWFS_ROUND_UP((size + bias), NEWFS_BLKS_SZ(1));
    uint8_t* temp_content   = (uint8_t*)malloc(size_aligned);
    uint8_t* cur            = temp_content;
    newfs_driver_read(offset_aligned, temp_content, size_aligned);
    memcpy(temp_content + bias, in_content, size);
    // 2. 从对齐位置开始，循环写入对齐大小的数据到磁盘
    // lseek(NEWFS_DRIVER(), offset_aligned, SEEK_SET);
    ddriver_seek(NEWFS_DRIVER(), offset_aligned, SEEK_SET);
    while (size_aligned != 0)
    {
        // write(NEWFS_DRIVER(), cur, NEWFS_IO_SZ());
        ddriver_write(NEWFS_DRIVER(), cur, NEWFS_IO_SZ());
        cur          += NEWFS_IO_SZ();
        size_aligned -= NEWFS_IO_SZ();   
    }

    free(temp_content);
    return NEWFS_ERROR_NONE;
}

/**
 * @brief 
 * 
 * @param inode 
 * @param dir [0...]
 * @return struct newfs_dentry* 
 */
struct newfs_dentry* newfs_get_dentry(struct newfs_inode * inode, int dir) {
    struct newfs_dentry* dentry_cursor = inode->dentrys;
    int    cnt = 0;
    while (dentry_cursor)
    {
        if (dir == cnt) {
            return dentry_cursor;
        }
        cnt++;
        dentry_cursor = dentry_cursor->brother;
    }
    return NULL;
}

/**
 * @brief 将dentry从inode的dentrys中取出
 * 
 * @param inode 一个目录的索引结点
 * @param dentry 该目录下的一个目录项
 * @return int 
 */
int newfs_drop_dentry(struct newfs_inode * inode, struct newfs_dentry * dentry) {
    boolean is_find = FALSE;
    struct newfs_dentry* dentry_cursor;
    dentry_cursor = inode->dentrys;
    
    if (dentry_cursor == dentry) {
        inode->dentrys = dentry->brother;
        is_find = TRUE;
    }
    else {
        while (dentry_cursor)
        {
            if (dentry_cursor->brother == dentry) {
                dentry_cursor->brother = dentry->brother;
                is_find = TRUE;
                break;
            }
            dentry_cursor = dentry_cursor->brother;
        }
    }
    if (!is_find) {
        return -NEWFS_ERROR_NOTFOUND;
    }
    inode->dir_cnt--;
    return inode->dir_cnt;
}

/**
 * @brief 删除内存中的一个inode
 * Case 1: Reg File
 * 
 *                  Inode
 *                /      \
 *            Dentry -> Dentry (Reg Dentry)
 *                       |
 *                      Inode  (Reg File)
 * 
 *  1) Step 1. Erase Bitmap     
 *  2) Step 2. Free Inode                      (Function of newfs_drop_inode)
 * ------------------------------------------------------------------------
 *  3) *Setp 3. Free Dentry belonging to Inode (Outsider)
 * ========================================================================
 * Case 2: Dir
 *                  Inode
 *                /      \
 *            Dentry -> Dentry (Dir Dentry)
 *                       |
 *                      Inode  (Dir)
 *                    /     \
 *                Dentry -> Dentry
 * 
 *   Recursive
 * @param inode 
 * @return int 
 */
int newfs_drop_inode(struct newfs_inode * inode) {
    struct newfs_dentry*  dentry_cursor;
    struct newfs_dentry*  dentry_to_free;
    struct newfs_inode*   inode_cursor;

    int byte_cursor = 0; 
    int bit_cursor  = 0; 
    int ino_cursor  = 0;
    int data_cursor = 0;
    boolean is_find = FALSE;

    if (inode == newfs_super.root_dentry->inode) {
        return NEWFS_ERROR_INVAL;
    }
    // simple只有目录项需要递归
    //  文件以及链接需要删除数据位图
    // newfs 目录项递归
    // 都要删除数据位图
    if (NEWFS_IS_DIR(inode)) {
        dentry_cursor = inode->dentrys;
                                                      /* 递归向下drop */
        while (dentry_cursor)
        {   
            inode_cursor = dentry_cursor->inode;
            newfs_drop_inode(inode_cursor);
            newfs_drop_dentry(inode, dentry_cursor);
            dentry_to_free = dentry_cursor;
            dentry_cursor = dentry_cursor->brother;
            free(dentry_to_free);
        }
    }
    for (byte_cursor = 0; byte_cursor < NEWFS_BLKS_SZ(newfs_super.map_inode_blks); 
        byte_cursor++)                            /* 调整inodemap */
    {
        for (bit_cursor = 0; bit_cursor < UINT8_BITS; bit_cursor++) {
            if (ino_cursor == inode->ino) {
                    newfs_super.map_inode[byte_cursor] &= (uint8_t)(~(0x1 << bit_cursor));
                    is_find = TRUE;
                    break;
            }
            ino_cursor++;
        }
        if (is_find == TRUE) {
            break;
        }
    }

    for (byte_cursor = 0; byte_cursor < NEWFS_BLKS_SZ(newfs_super.map_data_blks); 
        byte_cursor++)                            /* 调整datamap */
    {
        for (bit_cursor = 0; bit_cursor < UINT8_BITS; bit_cursor++) {
            if (data_cursor == inode->ino) {
                    newfs_super.map_data[byte_cursor] &= (uint8_t)(~(0x1 << bit_cursor));
                    is_find = TRUE;
                    break;
            }
            data_cursor++;
        }
        if (is_find == TRUE) {
            break;
        }
    }

    for (int i = 0; i < inode->data_block_nums; i++) {
        if (NEWFS_IS_REG(inode)) {
            if (inode->data_in_mem[i])
                free(inode->data_in_mem[i]);
        }
    }
    if (inode->data_in_mem)
        free(inode->data_in_mem);
    free(inode);
    return NEWFS_ERROR_NONE;
}