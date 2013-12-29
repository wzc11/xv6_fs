#include "types.h"
#include "defs.h"
#include "param.h"
#include "stat.h"
#include "mmu.h"
#include "fat32.h"
#include "fat_inode.h"
#include "param.h"
#include "inode.h"

struct inode *
fat_get_root(){
	cprintf("enter fat_get_root\n");
	struct inode *node;
	int fat_rootdev_num = 2;// set as 2...
	cprintf("before fat_iget\n");
	node = fat_iget(fat_rootdev_num, 2, 0);
	cprintf("after fat_iget\n");
	struct fat_inode *sin = vop_info(node, fat_inode);
	cprintf("inum = %d\n", sin->inum);
	return node;
}

// this file added 12.25