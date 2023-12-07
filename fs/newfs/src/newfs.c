#define _XOPEN_SOURCE 700

#include "newfs.h"

/******************************************************************************
* SECTION: 宏定义
*******************************************************************************/
#define OPTION(t, p)        { t, offsetof(struct custom_options, p), 1 }

/******************************************************************************
* SECTION: 全局变量
*******************************************************************************/
static const struct fuse_opt option_spec[] = {		/* 用于FUSE文件系统解析参数 */
	OPTION("--device=%s", device),
	FUSE_OPT_END
};

struct custom_options newfs_options;			 /* 全局选项 */
struct newfs_super super; 
/******************************************************************************
* SECTION: FUSE操作定义
*******************************************************************************/
static struct fuse_operations operations = {
	.init = newfs_init,						 /* mount文件系统 */		
	.destroy = newfs_destroy,				 /* umount文件系统 */
	.mkdir = newfs_mkdir,					 /* 建目录，mkdir */
	.getattr = newfs_getattr,				 /* 获取文件属性，类似stat，必须完成 */
	.readdir = newfs_readdir,				 /* 填充dentrys */
	.mknod = newfs_mknod,					 /* 创建文件，touch相关 */
	.write = NULL,								  	 /* 写入文件 */
	.read = NULL,								  	 /* 读文件 */
	.utimens = newfs_utimens,				 /* 修改时间，忽略，避免touch报错 */
	.truncate = NULL,						  		 /* 改变文件大小 */
	.unlink = NULL,							  		 /* 删除文件 */
	.rmdir	= NULL,							  		 /* 删除目录， rm -r */
	.rename = NULL,							  		 /* 重命名，mv */

	.open = NULL,							
	.opendir = NULL,
	.access = NULL
};
/******************************************************************************
* SECTION: 必做函数实现
*******************************************************************************/

/**
 * @brief 获取文件名
 * y
 * @param path 
 * @return char* 
 */
char* newfs_get_fname(const char* path) {
    char ch = '/';
    char *q = strrchr(path, ch) + 1;
    return q;
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
 * @brief 驱动读
 * 
 * @param offset 
 * @param out_content 
 * @param size 
 * @return int 
 */
int newfs_driver_read(int offset, uint8_t *out_content, int size) {
    int      offset_aligned = NEWFS_ROUND_DOWN(offset, NEWFS_IO_SZ());
    int      bias           = offset - offset_aligned;
    int      size_aligned   = NEWFS_ROUND_UP((size + bias), NEWFS_IO_SZ());
    uint8_t* temp_content   = (uint8_t*)malloc(size_aligned);
    uint8_t* cur            = temp_content;
    // lseek(NEWFS_DRIVER(), offset_aligned, SEEK_SET);
    ddriver_seek(NEWFS_DRIVER(), offset_aligned, SEEK_SET);
    while (size_aligned != 0)
    {
        // read(NEWFS_DRIVER(), cur, NEWFS_IO_SZ());
        ddriver_read(NEWFS_DRIVER(), cur, NEWFS_IO_SZ());
        cur          += NEWFS_IO_SZ();
        size_aligned -= NEWFS_IO_SZ();   
    }
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
    int      offset_aligned = NEWFS_ROUND_DOWN(offset, NEWFS_IO_SZ());
    int      bias           = offset - offset_aligned;
    int      size_aligned   = NEWFS_ROUND_UP((size + bias), NEWFS_IO_SZ());
    uint8_t* temp_content   = (uint8_t*)malloc(size_aligned);
    uint8_t* cur            = temp_content;
    newfs_driver_read(offset_aligned, temp_content, size_aligned);
    memcpy(temp_content + bias, in_content, size);
    
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
 * @brief 为一个inode分配dentry，采用头插法
 * 
 * @param inode 
 * @param dentry 
 * @return int 
 */
int newfs_alloc_dentry(struct newfs_inode* inode, struct newfs_dentry* dentry) {
    if (inode->dentrys == NULL) {
        inode->dentrys = dentry;
    }
    else {
        dentry->brother = inode->dentrys;
        inode->dentrys = dentry;
    }
    inode->dir_cnt++;
    // 插入是size增加
    inode->size += sizeof(struct newfs_dentry);
    return inode->dir_cnt;
}
/**
 * @brief 将dentry从inode的dentrys中取出
 * 
 * @param inode 
 * @param dentry 
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

    for (byte_cursor = 0; byte_cursor < NEWFS_BLKS_SZ(super.ino_map_blks); 
         byte_cursor++)
    {
        for (bit_cursor = 0; bit_cursor < UINT8_BITS; bit_cursor++) {
            if((super.map_inode[byte_cursor] & (0x1 << bit_cursor)) == 0) {    
                                                      /* 当前ino_cursor位置空闲 */
                super.map_inode[byte_cursor] |= (0x1 << bit_cursor);
                is_find_free_entry = TRUE;           
                break;
            }
            ino_cursor++;
        }
        if (is_find_free_entry) {
            break;
        }
    }

    if (!is_find_free_entry || ino_cursor == super.ino_max)
        return -NEWFS_ERROR_NOSPACE;

    inode = (struct newfs_inode*)malloc(sizeof(struct newfs_inode));
    inode->ino  = ino_cursor; 
    inode->size = 0;
                                                      /* dentry指向inode */
    dentry->inode = inode;
    dentry->ino   = inode->ino;
                                                      /* inode指回dentry */
    inode->dentry = dentry;
    
    inode->dir_cnt = 0;
    inode->dentrys = NULL;

    for (int i = 0; i < NEWFS_DATA_PER_FILE; i++) {
         inode->data[i] = (uint8_t *)malloc(NEWFS_BLK_SZ());
         inode->blk_no[i] = -1;
    }

    return inode;
}

/**
 * @brief 分配一个data，占用位图
 *
 * @param dentry 该dentry指向分配的data
 */
int newfs_alloc_datablk(struct newfs_dentry *dentry)
{
   struct newfs_inode *inode;
   int byte_cursor = 0;
   int bit_cursor = 0;
   int data_cursor = 0;
   boolean is_find_free_entry = FALSE;

   for (byte_cursor = 0; byte_cursor < NEWFS_BLKS_SZ(super.data_map_blks);
        byte_cursor++)
   {
      for (bit_cursor = 0; bit_cursor < UINT8_BITS; bit_cursor++)
      {
         if ((super.map_data[byte_cursor] & (0x1 << bit_cursor)) == 0)
         {
            /* 当前data_cursor位置空闲 */
            super.map_data[byte_cursor] |= (0x1 << bit_cursor);
            is_find_free_entry = TRUE;
            break;
         }
         data_cursor++;
      }
      if (is_find_free_entry)
      {
         break;
      }
   }

   // 位图满了，分配不了
   if (!is_find_free_entry || data_cursor == super.data_blks){
      printf("位图满了，分配不了");
      return -NEWFS_ERROR_NOSPACE;
   }
      

   // 根据data_cursor分配新的数据块
   inode = dentry->inode;
   
   // 找到没有索引的指针，赋值索引
   int i = 0;
   for (; i < NEWFS_DATA_PER_FILE; i++) {
      if(inode->blk_no[i] == -1){
         inode->blk_no[i] = data_cursor;
         break;
      }
         
   }
   
   // 最多只能有NEWFS_DATA_PER_FILE个数据块
   if(i == NEWFS_DATA_PER_FILE){
      return -NEWFS_ERROR_NOSPACE;
   }

   return 0;
   
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
    memcpy(inode_d.target_path, inode->target_path, NEWFS_MAX_FILE_NAME);
    inode_d.ftype       = inode->dentry->ftype;
    inode_d.dir_cnt     = inode->dir_cnt;
    int offset;
    for (int i = 0; i< NEWFS_DATA_PER_FILE; i++) {
      inode_d.blk_no[i] = inode->blk_no[i];
   }
    
    if (newfs_driver_write(NEWFS_INO_OFS(ino), (uint8_t *)&inode_d, 
                     sizeof(struct newfs_inode_d)) != NEWFS_ERROR_NONE) {
        NEWFS_DBG("[%s] io error\n", __func__);
        return -NEWFS_ERROR_IO;
    }
                                                      /* Cycle 1: 写 INODE */
                                                      /* Cycle 2: 写 数据 */
    if (NEWFS_IS_DIR(inode)) {                          
        dentry_cursor = inode->dentrys;
        int offset_limit;
        for (int i = 0; i < NEWFS_DATA_PER_FILE; i++) {
         offset = NEWFS_DATA_OFS(inode->blk_no[i]);
         offset_limit = NEWFS_DATA_OFS(inode->blk_no[i] + 1);
         while (dentry_cursor != NULL && offset + sizeof(struct newfs_dentry_d) < offset_limit)
         {
            // 写回dentry
            memcpy(dentry_d.fname, dentry_cursor->fname, NEWFS_MAX_FILE_NAME);
            dentry_d.ftype = dentry_cursor->ftype;
            dentry_d.ino = dentry_cursor->ino;
            if (newfs_driver_write(offset, (uint8_t *)&dentry_d,
                                 sizeof(struct newfs_dentry_d)) != NEWFS_ERROR_NONE)
            {
               NEWFS_DBG("[%s] io error\n", __func__);
               return -NEWFS_ERROR_IO;
            }

            // 递归调用
            if (dentry_cursor->inode != NULL)
            {
               newfs_sync_inode(dentry_cursor->inode);
            }

            dentry_cursor = dentry_cursor->brother;
            offset += sizeof(struct newfs_dentry_d);
         }
      }
    }
    else if (NEWFS_IS_REG(inode)) {
        for (int j = 0; j < NEWFS_DATA_PER_FILE; j++) {
         if (inode->blk_no[j] != -1) {
            if (newfs_driver_write(NEWFS_DATA_OFS(inode->blk_no[j]), (uint8_t *)inode->data[j],
                          NEWFS_BLK_SZ()) != NEWFS_ERROR_NONE)
            {
               NEWFS_DBG("[%s] io error\n", __func__);
               return -NEWFS_ERROR_IO;
            }
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
    printf("~ hello ~\n");
    struct newfs_inode* inode = (struct newfs_inode*)malloc(sizeof(struct newfs_inode));
    struct newfs_inode_d inode_d;
    struct newfs_dentry* sub_dentry;
    struct newfs_dentry_d dentry_d;
    int    dir_cnt = 0, i;
    if (newfs_driver_read(NEWFS_INO_OFS(ino), (uint8_t *)&inode_d, 
                        sizeof(struct newfs_inode_d)) != NEWFS_ERROR_NONE) {
        NEWFS_DBG("[%s] io error\n", __func__);
        return NULL;                    
    }
    inode->dir_cnt = 0;
    inode->ino = inode_d.ino;
    inode->size = inode_d.size;
    memcpy(inode->target_path, inode_d.target_path, NEWFS_MAX_FILE_NAME);
    inode->dentry = dentry;
    inode->dentrys = NULL;
    for (int j = 0; j < NEWFS_DATA_PER_FILE; j++) {
      inode->blk_no[j] = inode_d.blk_no[j];
   }

    if (NEWFS_IS_DIR(inode)) {
        dir_cnt = inode_d.dir_cnt;
        int offset, offset_limit;
        for (int j = 0; j < NEWFS_DATA_PER_FILE; j++) {
         if(dir_cnt == 0) break;

         offset = NEWFS_DATA_OFS(inode->blk_no[j]);  
         offset_limit = NEWFS_DATA_OFS(inode->blk_no[j] + 1);
         while(dir_cnt > 0 && offset + sizeof(struct newfs_dentry_d) < offset_limit){

            if (newfs_driver_read(offset, (uint8_t *)&dentry_d,
                              sizeof(struct newfs_dentry_d)) != NEWFS_ERROR_NONE)
            {
               NEWFS_DBG("[%s] io error\n", __func__);
               return NULL;
            }

            sub_dentry = new_dentry(dentry_d.fname, dentry_d.ftype);
            sub_dentry->parent = inode->dentry;
            sub_dentry->ino = dentry_d.ino;
            newfs_alloc_dentry(inode, sub_dentry);
                
            offset += sizeof(struct newfs_dentry_d);
            dir_cnt--;
         }
      }
    }
    else if (NEWFS_IS_REG(inode)) {
        for (int j = 0; j < NEWFS_DATA_PER_FILE; j++) {
         inode->data[j] = (uint8_t *)malloc(NEWFS_BLK_SZ());

         if (newfs_driver_read(NEWFS_DATA_OFS(inode->blk_no[j]), (uint8_t *)inode->data[j],
                        NEWFS_BLK_SZ()) != NEWFS_ERROR_NONE)
         {
            NEWFS_DBG("[%s] io error\n", __func__);
            return NULL;
         }
         
      }
    }
    return inode;
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
 * @brief 
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
 * @param path 
 * @return struct newfs_inode* 
 */
struct newfs_dentry* newfs_lookup(const char * path, boolean* is_find, boolean* is_root) {
    struct newfs_dentry* dentry_cursor = super.root_dentry;
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
        dentry_ret = super.root_dentry;
    }
    fname = strtok(path_cpy, "/");       
    while (fname)
    {   
        lvl++;
        if (dentry_cursor->inode == NULL) {           /* Cache机制 */
            newfs_read_inode(dentry_cursor, dentry_cursor->ino);
        }

        inode = dentry_cursor->inode;

        if (NEWFS_IS_REG(inode) && lvl < total_lvl) {
            NEWFS_DBG("[%s] not a dir\n", __func__);
            dentry_ret = inode->dentry;
            break;
        }
        if (NEWFS_IS_DIR(inode)) 
        {
            dentry_cursor = inode->dentrys;
            is_hit        = FALSE;

            while (dentry_cursor)
            {
                if (memcmp(dentry_cursor->fname, fname, strlen(fname)) == 0) {
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
 * @brief 挂载（mount）文件系统
 * 
 * @param conn_info 可忽略，一些建立连接相关的信息 
 * @return void*
 */
void* newfs_init(struct fuse_conn_info * conn_info) {
	/* TODO: 在这里进行挂载 */

	struct newfs_dentry*  root_dentry;
	struct newfs_super_d  super_d;
	boolean is_init = FALSE;
	struct newfs_inode*   root_inode;

	super.is_mounted = FALSE;
	super.fd = ddriver_open(newfs_options.device);
	if (super.fd < 0)
	{
		/* code */
		return super.fd;
	}

	ddriver_ioctl(NEWFS_DRIVER(), IOC_REQ_DEVICE_SIZE, &super.sz_disk);
	ddriver_ioctl(NEWFS_DRIVER(), IOC_REQ_DEVICE_IO_SZ, &super.sz_io);
	super.sz_blk = super.sz_io * 2;
	root_dentry = new_dentry("/", NEWFS_DIR);

	// 从磁盘中读出超级块
	if (newfs_driver_read(NEWFS_SUPER_OFS, (uint8_t *)(&super_d), 
                        sizeof(struct newfs_super_d)) != NEWFS_ERROR_NONE) {
        return -NEWFS_ERROR_IO;
    }

	/* 读取super */
	if(super_d.magic != NEWFS_MAGIC_NUM) { /* 幻数无 */
		// 重新估算磁盘布局信息
		super_d.sb_offset = 0;
		super_d.sb_blks = 1;
		
		super_d.ino_map_offset = super_d.sb_offset + NEWFS_BLKS_SZ(super_d.sb_blks);
		super_d.ino_map_blks = 1;
		
		super_d.data_map_offset = super_d.ino_map_offset + NEWFS_BLKS_SZ(super_d.ino_map_blks);
		super_d.data_map_blks = 1;
		
		super_d.ino_offset = super_d.data_map_offset + NEWFS_BLKS_SZ(super_d.data_map_blks);
		super_d.ino_blks = super.sz_disk / ((NEWFS_INODE_PER_FILE + NEWFS_DATA_PER_FILE) * NEWFS_BLK_SZ());
		
		super_d.data_offset = super_d.ino_offset + NEWFS_BLKS_SZ(super_d.ino_blks);
		super_d.data_blks = super.sz_disk / super.sz_blk - 1 - 1 - 1 - super_d.ino_blks;

		super_d.ino_max = super_d.ino_blks - super_d.sb_blks - super_d.ino_map_blks - super_d.data_map_blks;
		super_d.file_max = NEWFS_DATA_PER_FILE * NEWFS_BLK_SZ();

		super_d.sz_usage = 0;
		is_init = TRUE;
	}
	/* 建立 in-memory 结构 */
	// 填充磁盘布局信息
	super.sz_usage = super_d.sz_usage;

	super.sb_blks = super_d.sb_blks;
	super.sb_offset = super_d.sb_offset;

	super.ino_map_offset = super_d.ino_map_offset;
	super.ino_map_blks = super_d.ino_map_blks;

	super.data_map_blks = super_d.data_map_blks;
	super.data_map_offset = super_d.data_map_offset;

	super.ino_offset = super_d.ino_offset;
	super.ino_blks = super_d.ino_blks;

	super.data_offset = super_d.data_offset;
	super.data_blks = super_d.data_blks;
1
	super.ino_max = super_d.ino_max;
	super.file_max = super_d.file_max;

	super.map_inode = (uint8_t *)malloc(NEWFS_BLKS_SZ(super_d.ino_map_blks));
	super.map_data = (uint8_t *)malloc(NEWFS_BLKS_SZ(super_d.data_map_blks));

	// 读取索引节点
    if (newfs_driver_read(super_d.ino_map_offset, (uint8_t *)(super.map_inode), 
                        NEWFS_BLKS_SZ(super_d.ino_map_blks)) != NEWFS_ERROR_NONE) {
        return -NEWFS_ERROR_IO;
    }
	// 读取数据块节点
	if (newfs_driver_read(super_d.data_map_offset, (uint8_t *)(super.map_data), 
                        NEWFS_BLKS_SZ(super_d.data_map_blks)) != NEWFS_ERROR_NONE) {
        return -NEWFS_ERROR_IO;
    }

	/* 分配根节点 */
	// 创建空根目录和节点
	if (is_init) {                                    
        root_inode = newfs_alloc_inode(root_dentry);

        root_dentry->inode = root_inode;
        // 给根节点分配第一个数据块
	   if(newfs_alloc_datablk(root_dentry) < 0)
		   printf("新增数据块失败");
        
        newfs_sync_inode(root_inode);
    }

	/* 读取根目录inode, 生成层级 */
	root_inode            = newfs_read_inode(root_dentry, NEWFS_ROOT_INO);
    root_dentry->inode    = root_inode;
    super.root_dentry = root_dentry;
    super.is_mounted  = TRUE;
    
	return NULL;
}

/**
 * @brief 卸载（umount）文件系统
 * 
 * @param p 可忽略
 * @return void
 */
void newfs_destroy(void* p) {
	/* TODO: 在这里进行卸载 */
	struct newfs_super_d  super_d; 

    if (!super.is_mounted) {
        return NEWFS_ERROR_NONE;
    }

    newfs_sync_inode(super.root_dentry->inode);     /* 从根节点向下刷写节点 */

    super_d.magic = NEWFS_MAGIC_NUM;

    super_d.sz_usage = super.sz_usage;   

    super_d.sb_offset = super.sb_offset;
    super_d.sb_blks = super.sb_blks;

    super_d.ino_map_blks = super.ino_map_blks;
    super_d.ino_map_offset = super.ino_map_offset;

    super_d.data_map_offset = super.data_map_offset;
    super_d.data_map_blks = super.data_map_blks;

    super_d.ino_offset = super.ino_offset;
    super_d.ino_blks = super.ino_blks;

    super_d.data_blks = super.data_blks;
    super_d.data_offset = super.data_offset;

    super_d.ino_max = super.ino_max;
    super_d.file_max = super.file_max;
    
    // 写回超级块
    if (newfs_driver_write(NEWFS_SUPER_OFS, (uint8_t *)&super_d, 
                     sizeof(struct newfs_super_d)) != NEWFS_ERROR_NONE) {
        return -NEWFS_ERROR_IO;
    }
    // 写回索引位图
    if (newfs_driver_write(super_d.ino_map_offset, (uint8_t *)(super.map_inode), 
                         NEWFS_BLKS_SZ(super_d.ino_map_blks)) != NEWFS_ERROR_NONE) {
        return -NEWFS_ERROR_IO;
    }
    // 写回数据位图
    if (newfs_driver_write(super_d.data_map_offset, (uint8_t *)(super.map_data), 
                         NEWFS_BLKS_SZ(super_d.data_map_blks)) != NEWFS_ERROR_NONE) {
        return -NEWFS_ERROR_IO;
    }

    free(super.map_inode);
    free(super.map_data);
    ddriver_close(NEWFS_DRIVER());

    return NEWFS_ERROR_NONE;
}

/**
 * @brief 创建目录
 * 
 * @param path 相对于挂载点的路径
 * @param mode 创建模式（只读？只写？），可忽略
 * @return int 0成功，否则失败
 */
int newfs_mkdir(const char* path, mode_t mode) {
	/* TODO: 解析路径，创建目录 */
	(void)mode;
	boolean is_find, is_root;
	char* fname;
	struct newfs_dentry* last_dentry = newfs_lookup(path, &is_find, &is_root);
	struct newfs_dentry* dentry;
	struct newfs_inode*  inode;
	if (is_find) {
		return -NEWFS_ERROR_EXISTS;
	}

	if (NEWFS_IS_REG(last_dentry->inode)) {
		return -NEWFS_ERROR_UNSUPPORTED;
	}

	fname  = newfs_get_fname(path);
	dentry = new_dentry(fname, NEWFS_DIR); 

	dentry->parent = last_dentry;
    
    
	inode  = newfs_alloc_inode(dentry);
    dentry->inode = inode;

	// 给新的索引结点分配第一个数据块
	if(newfs_alloc_datablk(dentry) < 0)
		printf("新增数据块失败");
	newfs_alloc_dentry(last_dentry->inode, dentry);
    // 我们可能还需要为新增的dentry预先申请一个新的父目录的数据块来供对应的
	// dentry_d写回磁盘时使用（如果父目录原来申请的数据块已经放满了）
	int size_aligned_before = NEWFS_ROUND_UP((last_dentry->inode->size), NEWFS_BLK_SZ());
	int size_aligned_after = NEWFS_ROUND_UP((last_dentry->inode->size + sizeof(struct newfs_dentry)), NEWFS_BLK_SZ());
	if(size_aligned_after != size_aligned_before) {
		// 需要给父目录增加新的数据块
		if(newfs_alloc_datablk(last_dentry) < 0)
			printf("父目录新增数据块失败");
	}
	return 0;
}

/**
 * @brief 获取文件或目录的属性，该函数非常重要
 * 
 * @param path 相对于挂载点的路径
 * @param newfs_stat 返回状态
 * @return int 0成功，否则失败
 */
int newfs_getattr(const char* path, struct stat * newfs_stat) {
	/* TODO: 解析路径，获取Inode，填充newfs_stat，可参考/fs/simplefs/NEWFS.c的NEWFS_getattr()函数实现 */
	boolean	is_find, is_root;

	/* 路径解析 */
	struct newfs_dentry* dentry = newfs_lookup(path, &is_find, &is_root);
	if (is_find == FALSE) {
		return -NEWFS_ERROR_NOTFOUND;
	}
	/* 结构体填充 */
	if (NEWFS_IS_DIR(dentry->inode)) {
		newfs_stat->st_mode = S_IFDIR | NEWFS_DEFAULT_PERM;
		newfs_stat->st_size = dentry->inode->dir_cnt * sizeof(struct newfs_dentry_d);
	}
	else if (NEWFS_IS_REG(dentry->inode)) {
		newfs_stat->st_mode = S_IFREG | NEWFS_DEFAULT_PERM;
		newfs_stat->st_size = dentry->inode->size;
	}
	else if (NEWFS_IS_SYM_LINK(dentry->inode)) {
		newfs_stat->st_mode = S_IFLNK | NEWFS_DEFAULT_PERM;
		newfs_stat->st_size = dentry->inode->size;
	}

	newfs_stat->st_nlink = 1;
	newfs_stat->st_uid 	 = getuid();
	newfs_stat->st_gid 	 = getgid();
	newfs_stat->st_atime   = time(NULL);
	newfs_stat->st_mtime   = time(NULL);
	newfs_stat->st_blksize = NEWFS_BLK_SZ();

	if (is_root) {
		newfs_stat->st_size	= super.sz_usage; 
		newfs_stat->st_blocks = NEWFS_DISK_SZ() / NEWFS_BLK_SZ();
		newfs_stat->st_nlink  = 2;		/* !特殊，根目录link数为2 */
	}
	return 0;
}

/**
 * @brief 遍历目录项，填充至buf，并交给FUSE输出
 * 
 * @param path 相对于挂载点的路径
 * @param buf 输出buffer
 * @param filler 参数讲解:
 * 
 * typedef int (*fuse_fill_dir_t) (void *buf, const char *name,
 *				const struct stat *stbuf, off_t off)
 * buf: name会被复制到buf中
 * name: dentry名字
 * stbuf: 文件状态，可忽略
 * off: 下一次offset从哪里开始，这里可以理解为第几个dentry
 * 
 * @param offset 第几个目录项？
 * @param fi 可忽略
 * @return int 0成功，否则失败
 */
int newfs_readdir(const char * path, void * buf, fuse_fill_dir_t filler, off_t offset,
			    		 struct fuse_file_info * fi) {
    /* TODO: 解析路径，获取目录的Inode，并读取目录项，利用filler填充到buf，可参考/fs/simplefs/NEWFS.c的NEWFS_readdir()函数实现 */
    boolean	is_find, is_root;
	int		cur_dir = offset;

	struct newfs_dentry* dentry = newfs_lookup(path, &is_find, &is_root);
	struct newfs_dentry* sub_dentry;
	struct newfs_inode* inode;
	if (is_find) {
		inode = dentry->inode;
		sub_dentry = newfs_get_dentry(inode, cur_dir);
		if (sub_dentry) {
			filler(buf, sub_dentry->fname, NULL, ++offset);
		}
		return NEWFS_ERROR_NONE;
	}
	return -NEWFS_ERROR_NONE;
}

/**
 * @brief 创建文件
 * 
 * @param path 相对于挂载点的路径
 * @param mode 创建文件的模式，可忽略
 * @param dev 设备类型，可忽略
 * @return int 0成功，否则失败
 */
int newfs_mknod(const char* path, mode_t mode, dev_t dev) {
	/* TODO: 解析路径，并创建相应的文件 */
	boolean	is_find, is_root;
	
	struct newfs_dentry* last_dentry = newfs_lookup(path, &is_find, &is_root);
	struct newfs_dentry* dentry;
	struct newfs_inode* inode;
	char* fname;
	
	if (is_find == TRUE) {
		return -NEWFS_ERROR_EXISTS;
	}

	fname = newfs_get_fname(path);
	
	if (S_ISREG(mode)) {
		dentry = new_dentry(fname, NEWFS_REG_FILE);
	}
	else if (S_ISDIR(mode)) {
		dentry = new_dentry(fname, NEWFS_DIR);
	}
	else {
		dentry = new_dentry(fname, NEWFS_REG_FILE);
	}
	dentry->parent = last_dentry;

	inode = newfs_alloc_inode(dentry);
    dentry->inode = inode;

	newfs_alloc_dentry(last_dentry->inode, dentry);

    // 我们可能还需要为新增的dentry预先申请一个新的父目录的数据块
	int size_aligned_before = NEWFS_ROUND_UP((last_dentry->inode->size), NEWFS_BLK_SZ());
	int size_aligned_after = NEWFS_ROUND_UP((last_dentry->inode->size + sizeof(struct newfs_dentry)), NEWFS_BLK_SZ());
	if(size_aligned_after != size_aligned_before) {
		// 需要给父目录增加新的数据块
		if(newfs_alloc_datablk(last_dentry) < 0)
			printf("父目录新增数据块失败");
	}
	return 0;
}

/**
 * @brief 修改时间，为了不让touch报错 
 * 
 * @param path 相对于挂载点的路径
 * @param tv 实践
 * @return int 0成功，否则失败
 */
int newfs_utimens(const char* path, const struct timespec tv[2]) {
	(void)path;
	return 0;
}
/******************************************************************************
* SECTION: FUSE入口
*******************************************************************************/
int main(int argc, char **argv)
{
    int ret;
	struct fuse_args args = FUSE_ARGS_INIT(argc, argv);

	newfs_options.device = strdup("TODO: 这里填写你的ddriver设备路径");

	if (fuse_opt_parse(&args, &newfs_options, option_spec, NULL) == -1)
		return -1;
	
	ret = fuse_main(args.argc, args.argv, &operations, NULL);
	fuse_opt_free_args(&args);
	return ret;
}