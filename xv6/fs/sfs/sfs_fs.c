#include "types.h"
#include "defs.h"
#include "param.h"
#include "stat.h"
#include "mmu.h"
#include "sfs.h"
#include "sfs_inode.h"
#include "param.h"
#include "inode.h"

struct inode *
sfs_get_root(){
	struct inode *node;
	node = sfs_iget(ROOTDEV, ROOTINO, T_DIR);
	return node;
}