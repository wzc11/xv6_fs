#ifndef __FS_VFS_INODE_H__
#define __FS_VFS_INODE_H__

#include "sfs_inode.h"
#include "fat_inode.h"//added 12.25

#include "spinlock.h"

struct inode {
    union {
        struct sfs_inode __sfs_inode_info;
        struct fat_inode __fat_inode_info;
    } in_info;
    int fstype;
    const struct inode_ops *in_ops;
};

struct icache_universal {
  struct spinlock lock;
  struct inode inode[NINODE];
};



#define __vop_info(node, type)                                      \
    ({                                                              \
        struct inode *__node = (node);                              \
        &(__node->in_info.__##type##_info);                         \
     })

#define vop_info(node, type)                                        __vop_info(node, type)

void inode_init(struct inode *node, const struct inode_ops *ops, int fstype);

#define VOP_MAGIC                           0x8c4ba476

struct inode_ops {
    unsigned long vop_magic;

    int (*vop_read)(struct inode *ip, char *dst, uint off, uint n);
    int (*vop_write)(struct inode *ip, char *src, uint off, uint n);
    void (*vop_fstat)(struct inode *ip, struct stat *st);
    struct inode* (*vop_ref_inc)(struct inode *ip);
    void (*vop_ref_dec)(struct inode *ip);
    struct inode* (*vop_namei)(struct inode *node, char *path);
    struct inode* (*vop_nameiparent)(struct inode *node, char *path, char *name);
    int (*vop_dirlink)(struct inode *dp, char *name, struct inode *originip);
    int (*vop_unlink)(struct inode *dp, char *name);
    struct inode* (*vop_dirlookup)(struct inode *dp, char *name, uint *poff);
    void (*vop_ilock)(struct inode *ip);
    void (*vop_iunlock)(struct inode *ip);
    void (*vop_iunlockput)(struct inode *ip);
    void (*vop_iupdate)(struct inode *ip);
    struct inode* (*vop_ialloc)(struct inode *dirnode, uint dev, short type);
    int (*vop_isdirempty)(struct inode *dp);
    struct inode* (*vop_link_inc)(struct inode *ip);
    struct inode* (*vop_link_dec)(struct inode *ip);
    struct inode* (*vop_create_inode)(struct inode *dirnode, short type, short major, short minor, char* name);
    int (*vop_open)(struct inode *node, int open_flags);
    short (*vop_gettype)(struct inode *node);
    uint (*vop_getdev)(struct inode *node);
    short (*vop_getnlink)(struct inode *node);
    int (*vop_getpath)(struct inode *node, char *path, int maxlen);
    short (*vop_getmajor)(struct inode *node);
    short (*vop_getminor)(struct inode *node);
};

#define __vop_op(node, sym)                                                                         \
    ({                                                                                              \
        struct inode *__node = (node);                                                              \
        __node->in_ops->vop_##sym;                                                                  \
     })

#define vop_read(ip, dst, off, n)                       (__vop_op(ip, read)(ip, dst, off, n))
#define vop_write(ip, src, off, n)                      (__vop_op(ip, write)(ip, src, off, n))
#define vop_fstat(ip, st)                               (__vop_op(ip, fstat)(ip, st))
#define vop_getmajor(node)                              (__vop_op(node, getmajor)(node))
#define vop_getminor(node)                              (__vop_op(node, getminor)(node))
#define vop_ref_inc(ip)                                 (__vop_op(ip, ref_inc)(ip))
#define vop_ref_dec(ip)                                 (__vop_op(ip, ref_dec)(ip))
#define vop_namei(node, path)                           (__vop_op(node, namei)(node, path))
#define vop_nameiparent(node, path, name)               (__vop_op(node, nameiparent)(node, path, name))
#define vop_dirlink(dp, name, originip)                 (__vop_op(dp, dirlink)(dp, name, originip))
#define vop_unlink(dp, name)                            (__vop_op(dp, unlink)(dp, name))
#define vop_dirlookup(dp, name, poff)                   (__vop_op(dp, dirlookup)(dp, name, poff))
#define vop_ilock(ip)                                   (__vop_op(ip, ilock)(ip))
#define vop_iunlock(ip)                                 (__vop_op(ip, iunlock)(ip))
#define vop_iunlockput(ip)                              (__vop_op(ip, iunlockput)(ip))
#define vop_iupdate(ip)                                 (__vop_op(ip, iupdate)(ip))
#define vop_ialloc(dirnode, dev, type)                  (__vop_op(dirnode, ialloc)(dirnode, dev, type))
#define vop_isdirempty(dp)                              (__vop_op(ip, isdirempty)(dp))
#define vop_link_inc(ip)                                (__vop_op(ip, link_inc)(ip))
#define vop_link_dec(ip)                                (__vop_op(ip, link_dec)(ip))
#define vop_create_inode(dirnode, type, major, minor, name)   (__vop_op(dirnode, create_inode)(dirnode, type, major, minor, name))
#define vop_open(node, open_flags)                      (__vop_op(ip, open)(node, open_flags))
#define vop_gettype(node)                               (__vop_op(node, gettype)(node))
#define vop_getdev(node)                                (__vop_op(node, getdev)(node))
#define vop_getnlink(node)                              (__vop_op(node, getnlink)(node))
#define vop_getpath(node, path, maxlen)                 (__vop_op(node, getpath)(node, path, maxlen))

#define vop_init(node, ops, fstype)            inode_init(node, ops, fstype)

#endif