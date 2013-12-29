#include "types.h"
#include "defs.h"
#include "param.h"
#include "stat.h"
#include "mmu.h"
#include "sfs.h"
#include "fat32.h"//added 12.25
#include "proc.h"
#include "inode.h"
#include "vfs.h"

int 
vfs_get_curdir(struct inode **dir_store){
//    cprintf("enter vfs_get_curdir\n");
    struct inode *node;
    node = proc -> cwd;
    vop_ref_inc(node);
    *dir_store = node;
    return 0;
}

int 
vfs_get_root(const char *devname, struct inode **node_store) {
    struct inode *rooti;
//	if(devname[0] == 'x'){
        rooti = fat_get_root();
//	}
    *node_store = rooti;
    return 0;
}

int
vfs_get_bootfs(struct inode **node_store) {
    struct inode *rooti;
    rooti = fat_get_root();

    *node_store = rooti;
    return 0;
}

int
namecmp(const char *s, const char *t)
{
  return strncmp(s, t, DIRSIZ);
}

static int
get_device(char *path, char **subpath, struct inode **node_store) {
    int i, slash = -1, colon = -1;
    for (i = 0; path[i] != '\0'; i ++) {
//        if (path[i] == ':') { colon = i; break; }
        if (path[i] == '/') { slash = i; break; }
    }
    if (colon < 0 && slash != 0) {
        /* *
         * No colon before a slash, so no device name specified, and the slash isn't leading
         * or is also absent, so this is a relative path or just a bare filename. Start from
         * the current directory, and use the whole thing as the subpath.
         * */
        *subpath = path;
        return vfs_get_curdir(node_store);
    }
    if (colon > 0) {
        /* device:path - get root of device's filesystem */
        path[colon] = '\0';

        /* device:/path - skip slash, treat as device:path */
        while (path[++ colon] == '/');
        *subpath = path + colon;
        return vfs_get_root(path, node_store);
    }

    int ret;
    if (*path == '/') {
        if ((ret = vfs_get_bootfs(node_store)) != 0) {
            return ret;
        }
    }

    /* ///... or :/... */
    while (*(++ path) == '/');
    *subpath = path;
    return 0;
}

/*
 * vfs_lookup - get the inode according to the path filename
 */
struct inode*
vfs_lookup(char *path) {
    int ret;
    struct inode *node;
    if ((ret = get_device(path, &path, &node)) != 0) {
        return 0;
    }
    cprintf("vfs_lookup1 fstype = %d, path = %s\n", node->fstype, path);
    if (*path != '\0') {
        struct inode *result = vop_namei(node, path);
        cprintf("vfs_lookup getresult\n");
        return result;
    }
    cprintf("vfs_lookup2 fstype = %d\n", node->fstype);
    return node;
}

/*
 * vfs_lookup_parent - Name-to-vnode translation.
 *  (In BSD, both of these are subsumed by namei().)
 */
struct inode*
vfs_lookup_parent(char *path, char *name){
    int ret;
    struct inode *node;
    if ((ret = get_device(path, &path, &node)) != 0) {
        return 0;
    }
    cprintf("vfs_lookup_parent fstype = %d\n", node->fstype);
    return vop_nameiparent(node, path, name);
}

/*
 * vfs_getcwd - retrieve current working directory(cwd).
 */
int
vfs_getcwd(char *path, int len) {
    int ret;
    struct inode *node;
    if ((ret = vfs_get_curdir(&node)) != 0) {
        return ret;
    }
    ret = vop_getpath(node, path, len);
    return ret;
}