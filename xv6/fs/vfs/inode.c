#include "types.h"
#include "defs.h"
#include "param.h"
#include "stat.h"
#include "mmu.h"
#include "inode.h"

/* *
 * inode_init - initialize a inode structure
 * invoked by vop_init
 * */
void
inode_init(struct inode *node, const struct inode_ops *ops, int fstype) {
    node->in_ops = ops;
    node->fstype = fstype;
    vop_ref_inc(node);
}