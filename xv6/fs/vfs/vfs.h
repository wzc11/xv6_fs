struct inode;

int vfs_get_root(const char *devname, struct inode **node_store);
int vfs_get_curdir(struct inode **dir_store);
struct inode* vfs_lookup(char *path);
struct inode* vfs_lookup_parent(char *path, char *name);
int vfs_getcwd(char *path, int len);

int namecmp(const char *s, const char *t);
