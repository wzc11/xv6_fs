#ifndef __FS_VFS_VFS_H__
#define __FS_VFS_VFS_H__

struct inode;

struct fs {
    union {
        struct sfs_fs __sfs_info;                   
    } fs_info;                                     
    enum {
        fs_type_sfs_info,
    } fs_type;                                     
    int (*fs_sync)(struct fs *fs);                 
    struct inode *(*fs_get_root)(struct fs *fs);   
    int (*fs_unmount)(struct fs *fs);              
    void (*fs_cleanup)(struct fs *fs);           
};

#endif