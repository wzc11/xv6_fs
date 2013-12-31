#include "types.h"
#include "defs.h"
#include "param.h"
#include "stat.h"
#include "mmu.h"
#include "proc.h"
#include "spinlock.h"
#include "buf.h"
#include "file.h"//added 12.25
#include "fcntl.h"//added 12.25
#include "fat32.h"
#include "fat_inode.h"
#include "inode.h"
#include "vfs.h"
#include "x86.h"//added 12.27
#include "sfs_inode.h"//try 12.27

#define min(a, b) ((a) < (b) ? (a) : (b))
static void fat_itrunc(struct inode*);
static void fat_ilock(struct inode *ip);
static void fat_iunlock(struct inode *ip);
static const struct inode_ops fat_node_dirops;//modified 12.27
static const struct inode_ops fat_node_fileops;
extern struct icache_universal icache;//added 12.25

// struct {
//   struct spinlock lock;
//   struct inode inode[NINODE];
// } icache;//removed 12.25

// icache now is defined and externed in sfs_inode.c

/*
 * fat_get_ops - return function addr of fat_node_dirops/fat_node_fileops
 */
static const struct inode_ops *
fat_get_ops(uint type) {
//    cprintf("enter fat_get_ops type = %d, filetype = %d, dirtype=%d\n", type, T_FILE, T_DIR);
    switch (type) {
    case T_DIR:
        return &fat_node_dirops;
    case T_FILE:
        return &fat_node_fileops;
    case T_DEV:
        return &fat_node_fileops;
    }
    //panic("invalid file type %d.\n", type);
    panic("invalid file type in fat_get_ops(uint type), please check fat_inode.c");//modiefied 12.27
}

static short
fat_mapAttr(uchar attr)
{
  short type;
  switch(attr){
    case FAT_TYPE_DIR:
      type = T_DIR;break;
    case FAT_TYPE_FILE:
      type = T_FILE;break;
    case FAT_TYPE_DEV:
      type = T_DEV;break;
    default:
      type = attr;
      break;
  }
  return type;
}

static uchar
fat_mapType(short type)
{
  uchar attr;
  switch(type){
    case T_DIR:
      attr = FAT_TYPE_DIR;break;
    case T_FILE:
      attr = FAT_TYPE_FILE;break;
    case T_DEV:
      attr = FAT_TYPE_DEV;break;
    default:
      attr = type;
      break;
  }
  return attr;
}

void
copyshortname(char *dst, char *src)
{
  int i;
  for (i = 0; i != 11; ++i) {
    if (*src == 0x20)
      continue;
    *dst++ = *src++;
    if (i == 7)
      *dst++ = '.';
  }
  *dst++ = 0;
}

uchar fat_getBIOSsecond(){
  uchar data;
  outb(0x70,0x00);
  data=inb(0x71);
  return (data>>4)*10+(data & 0xf);
}

uchar fat_getBIOSminute(){
  uchar data;
  outb(0x70,0x02);
  data=inb(0x71);
  return (data>>4)*10+(data & 0xf);
}

uchar fat_getBIOShour(){
  uchar data;
  outb(0x70,0x04);
  data=inb(0x71);
  return ((data>>4)*10+(data & 0xf)+8)%24;
}

uchar fat_getBIOSday(){
  uchar data;
  outb(0x70,0x07);
  data=inb(0x71);
  return (data>>4)*10+(data & 0xf);
}

uchar fat_getBIOSmonth(){
  uchar data;
  outb(0x70,0x08);
  data=inb(0x71);
  return (data>>4)*10+(data & 0xf);
}

uchar fat_getBIOSyear(){
  ushort data;
  outb(0x70,0x09);
  data=inb(0x71);
  return (data>>4)*10+(data & 0xf);
}

// Some utilities

// get first sector number of a cluster
uint
fat_getFirstSectorofCluster(struct BPB* bpb, uint n)
{
  return (n - 2) * bpb->SecPerClus + bpb->ResvdSecCnt + bpb->NumFATs * bpb->FATSz32;
}

// get sector number and offset of a FAT entry mapped to cluster n
uint
fat_getFATEntry(struct BPB* bpb, uint n, uint* offset)
{
  *offset = n * 4;
  int ThisFATSecNum = bpb->ResvdSecCnt + (*offset / bpb->BytsPerSec);
  *offset %= bpb->BytsPerSec;
  return ThisFATSecNum;
}

// get Check Sum of short name
uchar
fat_getChkSum(uchar* pFcbName)
{
    ushort FcbNameLen;
    uchar Sum = 0;
    for (FcbNameLen=11; FcbNameLen > 0; --FcbNameLen) {
        Sum = ((Sum & 1) ? 0x80 : 0) + (Sum >> 1) + *pFcbName++;
    }
    return (Sum);
}

// get DIR type.
DIR_TYPE
fat_getDIRType(struct LDIR* ldir)
{
    if (ldir->Ord == 0xE5 || ldir->Ord == 0x00) //a deleted one is marked with 0xE5,an unused one is marked with 0x00
        return FAT_TYPE_EMPTY;
    if ((ldir->Attr & FAT_TYPE_LNMASK) == FAT_TYPE_LNAME)
        return FAT_TYPE_LNAME;
    switch (ldir->Attr & (FAT_TYPE_DIR | FAT_TYPE_VOLLBL)) {
        case 0x00:
            return FAT_TYPE_FILE;
        case FAT_TYPE_DIR:
            return FAT_TYPE_DIR;
        case FAT_TYPE_VOLLBL:
            return FAT_TYPE_VOLLBL;
        default:
            return FAT_TYPE_ERROR;
    }
}

// Read the BPB.
static void
fat_readbpb(int dev, struct BPB *bpb)
{
  struct buf *bp;
  bp = bread(dev, 0);
  memmove(bpb, bp->data, sizeof(*bpb));
//  cprintf("fat_readbpb, dev = %d, data[0] = %d, data[1] = %d, data[10] = %d, NumFATs=%d\n", dev, bp->data[0], bp->data[1], bp->data[10], bpb->NumFATs);
  brelse(bp);
}

// Update other FATs
void
fat_updateFATs(struct buf *sp)
{
  struct buf *tp;
  struct BPB bpb;
  int i, off;
  
  fat_readbpb(sp->dev, &bpb);
  for (i = 1, off = bpb.FATSz32; i < bpb.NumFATs; ++i, off += bpb.FATSz32) {
    tp = bread(sp->dev, sp->sector + off);
    memmove(tp->data, sp->data, 512);
    bwrite(tp);
    brelse(tp);
  }
}

// Clusters. 

// Allocate a disk cluster.
uint
fat_calloc(uint dev)
{
  uint c, cursect, lastsect, secOff;
  struct buf *bp, *bfsi;
  struct BPB bpb;
  struct FSI *fsi;

  fat_readbpb(dev, &bpb);
  bfsi = bread(dev, bpb.FSInfo);
  fsi = (struct FSI*)bfsi->data;  
//  cprintf("enter fatcalloc, dev = %d\n", dev);
  // Look for an empty cluster from fsi.Nxt_Free.
  bp = 0;
  lastsect = 0;
//  cprintf("Nxt_Free = %d, TotSec32 = %d, SecPerClus = %d\n", fsi->Nxt_Free, bpb.TotSec32, bpb.SecPerClus);
  for(c = fsi->Nxt_Free; c < bpb.TotSec32 / bpb.SecPerClus; ++c){
//    cprintf("cluster number = %d\n", c);
    cursect = fat_getFATEntry(&bpb, c, &secOff);
 //   cprintf("cluster number1 = %d\n", c);
    if (cursect != lastsect){ // Is this sector in memory?
      if (bp){
        brelse(bp);
      }
  //    cprintf("before bread dev = %d, cursect = %d\n", dev, cursect);
      bp = bread(dev, cursect);
      lastsect = cursect;
    }
//    cprintf("cluster number2 = %d\n", c);
    if (!*(uint*)(bp->data + secOff)){ // Is cluster free?
      // Mark cluster in use on disk.
      *(uint*)(bp->data + secOff) = LAST_FAT_ENTRY;
      fat_updateFATs(bp);
      bwrite(bp);
      brelse(bp);
      // Update FSInfo.
      ++fsi->Nxt_Free;
      --fsi->Free_Count;
      bwrite(bfsi);
      brelse(bfsi);
  //    cprintf("calloc:find c= %d\n", c);
      return c;
    }
  }
//  cprintf("calloc: cannot find\n");
  // Cannot find a free cluster from Nxt_Free.
  for(c = 2; c < fsi->Nxt_Free; ++c){
    cursect = fat_getFATEntry(&bpb, c, &secOff);
    if (cursect != lastsect){ // Is this sector in memory?
      if (bp)
        brelse(bp);
  //    cprintf("before bread2 dev = %d, cursect = %d\n", dev, cursect);
      bp = bread(dev, cursect);
      lastsect = cursect;
    }
    if (!*(uint*)(bp->data + secOff)){ // Is cluster free?
      // Mark cluster in use on disk.
      *(uint*)(bp->data + secOff) = LAST_FAT_ENTRY;
      fat_updateFATs(bp);
      bwrite(bp);
      brelse(bp);
      // Update FSInfo.
      fsi->Nxt_Free = c + 1;
      --fsi->Free_Count;
      bwrite(bfsi);
      brelse(bfsi);
   //   cprintf("calloc: cannot find\n");
      return c;
    }
  }
  panic("balloc: out of clusters");
}

//clear a cluster from cluster
void
fat_cclear(uint dev, uint cluster)
{
  struct buf *cp;
  struct BPB bpb;
  int i, sec;
  
  fat_readbpb(dev, &bpb);
  sec = fat_getFirstSectorofCluster(&bpb, cluster);
  for (i = 0; i < bpb.SecPerClus; ++i) {
 //   cprintf("before bread3 dev = %d, cursect = %d\n", dev, sec+i);
    cp = bread(dev, sec + i);
    memset(cp->data, 0, sizeof(cp->data));
    bwrite(cp);
    brelse(cp);
  }
}

// Inodes.

void
fat_iinit(void)
{
  initlock(&icache.lock, "icache");
}

// Copy inode, which has changed, from memory to disk.
void
fat_iupdate(struct inode *ip)
{ 
  struct fat_inode *sin = vop_info(ip, fat_inode); 
  uint curFatsect, lastFatsect = 0, secOff;
  uint si, s, cno = sin->dircluster;
  struct buf *fp, *sp;
  struct BPB bpb;
  struct DIR *de;
//  cprintf("iupdate1 \n");
  fat_readbpb(sin->dev, &bpb);
  fp = 0;
  do {
    s = fat_getFirstSectorofCluster(&bpb, cno);
    for (si = 0; si < bpb.SecPerClus; ++si) {   // Every sector
  //    cprintf("before bread4 cursect = %d\n", s+si);
      sp = bread(sin->dev, s + si);
 //     cprintf("iupdate2 data = %d\n", sp->data);
      for (de = (struct DIR*)sp->data;
           de < (struct DIR*)(sp->data + SECTSIZE);
           ++de) {          // Every entry
 //       cprintf("iupdate: FstClusHI = %d, Fstclulo = %d, inum = %d\n", de->FstClusHI, de->FstClusLO, sin->inum);
        if (((de->FstClusHI << 16) | de->FstClusLO) == sin->inum) {
          de->Attr = fat_mapType(sin->type);
          de->CrtDate = (ushort)sin->major;
          de->CrtTime = (ushort)sin->minor;
 //         cprintf("iupdate, major = %d, minor = %d\n", de->CrtDate, de->CrtTime);
          if (!sin->size && sin->type != T_DIR) { // Init size of file and creat time
            de->FileSize = 1;
            de->CrtTimeTenth = 0x5A;
          } else {
            de->FileSize = sin->size;
          }
 //         cprintf("iupdate3 \n");
          bwrite(sp);
          brelse(sp);
          if (fp)
            brelse(fp);
          return;
        }
      }
      brelse(sp);
    }
//    cprintf("iupdate4 \n");
    // Find FAT entry
    curFatsect = fat_getFATEntry(&bpb, cno, &secOff);
    if (curFatsect != lastFatsect) {
      if (fp)
        brelse(fp);
   //   cprintf("before bread5 cursect = %d\n", curFatsect);
      fp = bread(sin->dev, curFatsect);
      lastFatsect = curFatsect;
    }
    cno = *(uint*)(fp->data + secOff);
  } while (!isEOF(cno));
  panic("iupdate DIR entry not found");
}

// Find the inode with number inum on device dev
// and return the in-memory copy.
struct inode*
fat_iget(uint dev, uint inum, short type, uint dircluster)
{
  struct inode *ip, *empty;
  struct fat_inode *fip, *tip;////////////

  acquire(&icache.lock);

  // Try for cached inode.
  empty = 0;
  for(ip = &icache.inode[0]; ip < &icache.inode[NINODE]; ip++){
    fip = vop_info(ip, fat_inode);
//    cprintf("IGET: fstype = %d, ref = %d, dev = %d, inum = %d\n", ip->fstype, fip->ref, fip->dev, fip->inum);
    if(ip->fstype == FAT_INODE && fip->ref > 0 && fip->dev == dev && fip->inum == inum){
      fip->ref++;
      release(&icache.lock);
      return ip;
    }
    if(empty == 0 && fip->ref == 0)    // Remember empty slot.
      empty = ip;
  }

  // Allocate fresh inode.
  if(empty == 0)
    panic("iget: no inodes");

  ip = empty;
  tip = vop_info(ip, fat_inode);
  tip->dev = dev;
  tip->inum = inum;
  tip->ref = 1;
  tip->flags = 0;
  tip->dircluster = dircluster;
  release(&icache.lock);
  // below added 12.27
//  cprintf("fat_iget, type = %d\n", type);
  if(type != 0){
    tip->type = type;
  }else{
    fat_ilock(ip);
    fat_iunlock(ip);
  }
//  cprintf("tip type = %d, major = %d, minor = %d, nlink = %d, inum = %d\n", tip -> type, tip->major,tip->minor,tip->nlink, tip->inum);
  vop_init(ip, fat_get_ops(tip->type), FAT_INODE);

  return ip;
}//

// Increment reference count for ip.
// Returns ip to enable ip = idup(ip1) idiom.
struct inode*
fat_idup(struct inode *ip)
{
  struct fat_inode *sin = vop_info(ip, fat_inode); 
  acquire(&icache.lock);
  sin->ref++;
  release(&icache.lock);
  return ip;
}

// Lock the given inode.
static void
fat_ilock(struct inode *ip)
{
  struct fat_inode *sin = vop_info(ip, fat_inode); 
  if(sin == 0 || sin->ref < 1)
    panic("ilock");
  acquire(&icache.lock);
  while(sin->flags & I_BUSY)
    sleep(ip, &icache.lock);//
  sin->flags |= I_BUSY;
  release(&icache.lock);
  if (sin->inum == 2) { // Root file
    sin->flags |= I_VALID;
    sin->type = T_DIR;
    sin->nlink = 1;
    return;
  }
  if(!(sin->flags & I_VALID)){
    
    uint curFatsect, lastFatsect = 0, secOff;
    uint si, s, cno = sin->dircluster;
    struct buf *fp, *sp;
    struct BPB bpb;
    struct DIR *de;
  
    fat_readbpb(sin->dev, &bpb);
    fp = 0;
    do {
      s = fat_getFirstSectorofCluster(&bpb, cno);
 //     cprintf("secperclus = %d\n", bpb.SecPerClus);
      for (si = 0; si < bpb.SecPerClus; ++si) { // Every sector
 //       cprintf("secnum = %d\n", si);
  //      cprintf("before bread6 cursect = %d\n", s+si);
        sp = bread(sin->dev, s + si);
        for (de = (struct DIR*)sp->data;
             de < (struct DIR*)(sp->data + SECTSIZE);
             ++de) {          // Every entry
      //    cprintf("ilock: FstClusHI = %d, Fstclulo = %d, inum = %d\n", de->FstClusHI, de->FstClusLO, sin->inum);
          if (((de->FstClusHI << 16) | de->FstClusLO) == sin->inum) {
   //         cprintf("ilock3\n");
            sin->type = fat_mapAttr(de->Attr);
            sin->major = (short)de->CrtDate;
            sin->minor = (short)de->CrtTime;
            if ((de->FileSize == 1 && de->CrtTimeTenth == 0x5A))
              sin->size = 0;
            else
              sin->size = de->FileSize;
            sin->nlink = 1;
            brelse(sp);
            if (fp)
              brelse(fp);
            sin->flags |= I_VALID;
            if(sin->type == 0)
              panic("ilock: no type");
            return;
          }
        }
        brelse(sp);
      }
      // Find FAT entry
      curFatsect = fat_getFATEntry(&bpb, cno, &secOff);
      if (curFatsect != lastFatsect) {
        if (fp)
          brelse(fp);
    //    cprintf("before bread4 cursect = %d\n", curFatsect);
        fp = bread(sin->dev, curFatsect);
        lastFatsect = curFatsect;
      }
      cno = *(uint*)(fp->data + secOff);
    } while (!isEOF(cno));
    brelse(fp);
  }
  sin->flags |= I_VALID;
  //panic("ilock DIR entry not found");
}

// Unlock the given inode.
static void
fat_iunlock(struct inode *ip)
{
  struct fat_inode *sin = vop_info(ip, fat_inode); 
//  cprintf("flags = %d, ref = %d\n", sin->flags, sin->ref);
  if(sin == 0 || !(sin->flags & I_BUSY) || sin->ref < 1)
    panic("iunlock");

  acquire(&icache.lock);
  sin->flags &= ~I_BUSY;
  wakeup(ip);//
  release(&icache.lock);
}

// Caller holds reference to unlocked ip.  Drop reference.
void
fat_iput(struct inode *ip)
{
  struct fat_inode *sin = vop_info(ip, fat_inode);
  acquire(&icache.lock);
  if(sin->ref == 1 && (sin->flags & I_VALID) && sin->nlink == 0){
    // inode is no longer used: truncate and free inode.
    if(sin->flags & I_BUSY)
      panic("iput busy");
    sin->flags |= I_BUSY;
    release(&icache.lock);
    fat_itrunc(ip);/////////////////////////////////////
    sin->type = 0;
 //   fat_iupdate(ip);
    acquire(&icache.lock);
    sin->flags = 0;
    wakeup(ip);//
  }
  sin->ref--;
  release(&icache.lock);
}

// Common idiom: unlock, then put.
void
fat_iunlockput(struct inode *ip)
{
  //struct fat_inode *sin = vop_info(ip, fat_inode); 
  fat_iunlock(ip);//////////////////////////
  fat_iput(ip);////////////////////////////
}

// Inode contents

// Truncate inode (discard contents).
// Only called after the last dirent referring
// to this inode has been erased on disk.
static void
fat_itrunc(struct inode *ip)
{
  struct fat_inode *sin = vop_info(ip, fat_inode); 
  uint curFatsect, lastFatsect = 0, secOff;
  uint si, s, cno, chksum, cnoend, siend;
  struct buf *fp, *sp, *fsip;
  struct BPB bpb;
  struct FSI *fsi;
  struct DIR *de, *deend;

  fat_readbpb(sin->dev, &bpb);
  fp = 0;
  cno = sin->dircluster;
  do {
    s = fat_getFirstSectorofCluster(&bpb, cno);
    for (si = 0; si < bpb.SecPerClus; ++si) { // Every sector
 //     cprintf("before bread8 cursect = %d\n", s+si);
      sp = bread(sin->dev, s + si);
      for (de = (struct DIR*)sp->data;
           de < (struct DIR*)(sp->data + SECTSIZE);
           ++de) {  // Every entry
        if (((de->FstClusHI << 16) | de->FstClusLO) == sin->inum) {
          chksum = fat_getChkSum(de->Name);
          de->Name[0] = 0xE5;
          bwrite(sp);
          brelse(sp);
          if (fp)
            brelse(fp);
          cnoend = cno;
          siend = si;
          deend = de;
          goto longname;
        }
      }
      brelse(sp);
    }
    // Find FAT entry
    curFatsect = fat_getFATEntry(&bpb, cno, &secOff);
    if (curFatsect != lastFatsect) {
      if (fp) {
        bwrite(fp);
        brelse(fp);
      }
  //    cprintf("before bread9 cursect = %d\n", curFatsect);
      fp = bread(sin->dev, curFatsect);
      lastFatsect = curFatsect;
    }
    cno = *(uint*)(fp->data + secOff);
  } while (!isEOF(cno));
  panic("iupdate DIR entry not found");

longname:
  fp = 0;
  cno = sin->dircluster;
  do {
    s = fat_getFirstSectorofCluster(&bpb, cno);
    for (si = 0; si < bpb.SecPerClus; ++si) { // Every sector
  //    cprintf("before bread10 cursect = %d\n", s+si);
      sp = bread(sin->dev, s + si);
      for (de = (struct DIR*)sp->data;
           de < (struct DIR*)(sp->data + SECTSIZE);
           ++de) {  // Every entry
        if (((struct LDIR*)de)->ChkSum == chksum) {
          de->Name[0] = 0xE5;    
        }
        if(deend == de && siend == si && cnoend == cno){
          bwrite(sp);
          brelse(sp);
          if (fp)
            brelse(fp);
          goto fatentry;
        }
      }
      brelse(sp);
    }
    // Find FAT entry
    curFatsect = fat_getFATEntry(&bpb, cno, &secOff);
    if (curFatsect != lastFatsect) {
      if (fp)
        brelse(fp);
  //    cprintf("before bread11 cursect = %d\n", curFatsect);
      fp = bread(sin->dev, curFatsect);
      lastFatsect = curFatsect;
    }
    cno = *(uint*)(fp->data + secOff);
  } while (!isEOF(cno));
  if (fp)
    brelse(fp);

fatentry:
  fsip = bread(sin->dev, bpb.FSInfo);
  fsi = (struct FSI*)fsip->data; 
  cno = sin->inum;
  fp = 0;
  do{
    curFatsect = fat_getFATEntry(&bpb, cno, &secOff);
    if (curFatsect != lastFatsect){
      if (fp) {
        fat_updateFATs(fp);
        bwrite(fp);
        brelse(fp);
      }
 //     cprintf("before bread12 cursect = %d\n", curFatsect);
      fp = bread(sin->dev, curFatsect);
      lastFatsect = curFatsect;
    }
    fat_cclear(sin->dev, cno);
    cno = *(uint*)(fp->data + secOff);
    *(uint*)(fp->data + secOff) = 0;
    ++fsi->Free_Count;
  } while (!isEOF(cno));
  fat_updateFATs(fp);
  bwrite(fp);
  brelse(fp);
  bwrite(fsip);
  brelse(fsip);
  sin->size = 0;
}

// Copy stat information from inode.
void
fat_stati(struct inode *ip, struct stat *st)
{
  struct fat_inode *sin = vop_info(ip, fat_inode); 
  st->dev = sin->dev;
  st->ino = sin->inum;
  st->type = sin->type;
  st->nlink = sin->nlink;//added 12.25
  st->size = sin->size;
  st->fstype = ip->fstype;
}

// Read data from inode.
int
fat_readi(struct inode *ip, char *dst, uint off, uint n)
{
//  cprintf("start read\n");
  struct fat_inode *sin = vop_info(ip, fat_inode); 
  if(sin->type == T_DEV){
    if(sin->major < 0 || sin->major >= NDEV || !devsw[sin->major].read)
      return -1;
    return devsw[sin->major].read(ip, dst, n);
  }

  if(sin->type == T_DIR)
    n = 32;
  else {
    if(off > sin->size || off + n < off)
      return -1;
    if(off + n > sin->size)
      n = sin->size - off;
  }

  uint curFatsect, lastFatsect = 0, secOff;
  uint cno = sin->inum;
  uint s, pos = 0, tot = 0, si, m;
  uint clustersize;

  struct buf *fp, *sp;
  struct BPB bpb;

  fat_readbpb(sin->dev, &bpb);
  clustersize = bpb.SecPerClus * SECTSIZE;
  fp = 0;
  do {
    // If it is in this cluster
    if (off < pos + clustersize) {
      s = fat_getFirstSectorofCluster(&bpb, cno);
      for (si = (off - pos) / SECTSIZE; si < bpb.SecPerClus; ++si) {
   //     cprintf("before bread1, si = %d",si);
 //       cprintf("before bread13 cursect = %d\n", s+si);
        sp = bread(sin->dev, s + si);
        m = min(n - tot, SECTSIZE - off % SECTSIZE);    //make sure it is read completely
        memmove(dst, sp->data + off % SECTSIZE, m);
        brelse(sp);
        tot += m;
        off += m;
        dst += m;
        if (tot == n)
          goto finish;
      }
    }
    pos += clustersize;
    // Find FAT entry
    curFatsect = fat_getFATEntry(&bpb, cno, &secOff);
    if (curFatsect != lastFatsect) {
      if (fp)
        brelse(fp);
  //    cprintf("before bread14 cursect = %d\n", curFatsect);
      fp = bread(sin->dev, curFatsect);
      lastFatsect = curFatsect;
    }
    cno = *(uint*)(fp->data + secOff);
  } while (!isEOF(cno));
  n = 0;
 // cprintf("end read\n");
finish:
  if (fp)
    brelse(fp);
  return n;
}

// Write data to inode.
int
fat_writei(struct inode *ip, char *src, uint off, uint n)
{
//  cprintf("enter fat_writei\n");
  struct fat_inode *sin = vop_info(ip, fat_inode); 
  if(sin->type == T_DEV){
    if(sin->major < 0 || sin->major >= NDEV || !devsw[sin->major].write)
      return -1;
    return devsw[sin->major].write(ip, src, n);
  }
//  cprintf("enter fat_writei2\n");
  if(off > sin->size || off + n < off)
    return -1;

  uint curFatsect, lastFatsect = 0, secOff;
  uint cno = sin->inum;
  uint s, pos = 0, tot = 0, si, m;
  uint clustersize;

  struct buf *fp, *sp;
  struct BPB bpb;

  fat_readbpb(sin->dev, &bpb);
  clustersize = bpb.SecPerClus * SECTSIZE;
  fp = 0;
  do {
    // If it is in this cluster
    if (off < pos + clustersize) {
//      cprintf("in if\n");
      s = fat_getFirstSectorofCluster(&bpb, cno);
      for (si = (off - pos) / SECTSIZE; si < bpb.SecPerClus; ++si) {
//        cprintf("in for\n");
//        cprintf("before bread15 cursect = %d\n", s+si);
        sp = bread(sin->dev, s + si);
        m = min(n - tot, SECTSIZE - off % SECTSIZE);
        memmove(sp->data + off % SECTSIZE, src, m);
        bwrite(sp);
        brelse(sp);
        tot += m;
        off += m;
        src += m;
        if (tot == n)
          goto finish;
      }
    }
//    cprintf("enter fat_writei3\n");
    pos += clustersize;
    // Locate to FAT entry
    curFatsect = fat_getFATEntry(&bpb, cno, &secOff);
    if (curFatsect != lastFatsect) {
      if (fp) {
        fat_updateFATs(fp);
        bwrite(fp);
        brelse(fp);
      }
 //     cprintf("before bread16 cursect = %d\n", curFatsect);
      fp = bread(sin->dev, curFatsect);
      lastFatsect = curFatsect;
    }
//    cprintf("enter fat_writei4\n");
    cno = *(uint*)(fp->data + secOff);
    if (isEOF(cno)){
//      cprintf("end of cno\n");
      if(fp){
        brelse(fp);
      }
      cno = fat_calloc(sin->dev);
      fp = bread(sin->dev, curFatsect);
      *(uint*)(fp->data + secOff) = cno;
    }
//    cprintf("after is EOF\n");
  } while (1);

finish:
  if (fp) {
 //   cprintf("release fp\n");
    fat_updateFATs(fp);
    bwrite(fp);
    brelse(fp);
  }
  if(n > 0 && off > sin->size){
    sin->size = off;
    fat_iupdate(ip);
  }
  return n;
}

// Directories
char
fat_upper(char c)
{
  if (c >= 'a' && c <= 'z')
    return c - 32;
  return c;
}

int
fat_isvalid(char c)
{
  char set[] = "\"*+,./:;<=>?[\\]|";
  char *p;
  if (c <= 32 || c == 127)
    return 0;
  for (p = set; *p; ++p)
    if (c == *p)
      return 0;
  return 1;
}

//Short name
void
fat_getshortname(const char *s, char *namebuff)
{
  uint len;
  uint dotpos = FAT_DIRSIZ;
  uint namelen;
  uint extlen;
  uint i;

  for (i = 0; i < 11; ++i)
    namebuff[i] = 0x20;

  len = strlen(s);

  for (i = 0; i < len; ++i)
    if (s[i] == '.')
      dotpos = i;

  for (i = 0, namelen = 0; namelen < 6 && i < dotpos; ++i) {
    if (fat_isvalid(s[i]))
      namebuff[namelen++] = fat_upper(s[i]);
    else
      namebuff[namelen++] = '_';
  }

  namebuff[namelen++] = '~';
  namebuff[namelen++] = '1';

  if (dotpos < len)
    for (i = dotpos, extlen = 0; extlen < 3 && i < len; ++i) {
      if (fat_isvalid(s[i]))
        namebuff[namelen++] = fat_upper(s[i]);
      else
        namebuff[namelen++] = '_';
    }
}

void
fat_updatename(uchar *name)
{
  int i = 7;
  while (1) {
    if (name[i] == '~') {
      name[i] = '1';
      name[i - 1] = '~';
      break;
    }
    ++name[i];
    if (name[i] > '9')
      name[i--] = '0';
    else
      break;
  }
}

int
fat_namecmp(const char *s, const char *t)
{
  return strncmp(s, t, FAT_DIRSIZ);
}

// Look for a directory entry in a directory.
// Caller must have already locked dp.
struct inode*
fat_dirlookup(struct inode *dp, char *name, uint* poff)//modified 12.25
{
  struct fat_inode *fdp = vop_info(dp, fat_inode);
  if(fdp->type != T_DIR)
    panic("dirlookup not DIR");
  if(fdp->inum == 2 && strncmp(name, "..", 2) == 0){
    return dp;
  }
//  cprintf("dpinum = %d\n", fdp->inum);
  uint curFatsect, lastFatsect = 0, secOff;
  uint cno = fdp->inum, si, s, inum;
  struct buf *fp, *sp = 0;
  struct BPB bpb;
  struct LDIR *de;
  char namebuf[FAT_DIRSIZ + 1]  = {0};
  uchar chksum = 0;
  int ord = 0;
  int nbp = 0, i;
  
  fat_readbpb(fdp->dev, &bpb);
//  cprintf("fat_dirlookup1, ExtFlags= %d, FilSysType = %s\n", bpb.ExtFlags, bpb.FilSysType);
  fp = 0;
  do {
    s = fat_getFirstSectorofCluster(&bpb, cno);
  //  cprintf("after get first sector cno = %d\n", cno);
    for (si = 0; si < bpb.SecPerClus; ++si) {   // Every sector
  //    cprintf("before bread s + si = %d\n", s + si);
  //    cprintf("before bread17 cursect = %d\n", s+si);
      sp = bread(fdp->dev, s + si);
  //    cprintf("bread si = %d\n", si);
      for (de = (struct LDIR*)sp->data;
           de < (struct LDIR*)(sp->data + SECTSIZE);
           ++de) {          // Every entry
  //      cprintf("dirtype = %d\n", fat_getDIRType(de));
        switch (fat_getDIRType(de)) {

          case FAT_TYPE_VOLLBL:
          case FAT_TYPE_EMPTY:
            break;

          case FAT_TYPE_ERROR:
            panic("dirlookup wrong DIR entry");

          case FAT_TYPE_LNAME:
            if (de->Ord & FAT_TYPE_LLNMASK) {    // Last Entry
              nbp = FAT_DIRSIZ - 1;
              chksum = de->ChkSum;
              ord = de->Ord - FAT_TYPE_LLNMASK;
            } else if (chksum != de->ChkSum
                       || --ord != de->Ord)
              panic("dirlookup long filename wrong");
            for (i = 0; i < 2; ++i)
              namebuf[nbp - 2 + i] = (char)de->Name3[i];
            for (i = 0; i < 6; ++i)
              namebuf[nbp - 8 + i] = (char)de->Name2[i];
            for (i = 0; i < 5; ++i)
              namebuf[nbp - 13 + i] = (char)de->Name1[i];
            nbp -= 13;
            break;

          default:
    //        cprintf("name = %s, dename = %s\n", (char*)name, (char*)de);
            if (!fat_namecmp(name, namebuf + nbp)           // Long file name
                    || !strncmp((char*)name, (char*)de, 11)       // Short name with no \0
                    || !strncmp((char*)name, (char*)de, strlen(name))) {  // Short name with \0
              // Matches
      //        cprintf("matches\n");
              inum = (((struct DIR*)de)->FstClusHI << 16) | ((struct DIR*)de)->FstClusLO;
              if (!inum) {
                if (!strncmp("..", (char*)de, 2)) {         // Root file
                  inum = 2;
                } else {              // Empty file
                  inum = fat_calloc(fdp->dev);
                  ((struct DIR*)de)->FstClusHI = inum >> 16;
                  ((struct DIR*)de)->FstClusLO = (ushort)inum;
                  ((struct DIR*)de)->FileSize = 1;
                  ((struct DIR*)de)->CrtTimeTenth = 0x5A;
                  bwrite(sp);
                }
              }
              if (fp)
                brelse(fp);
              brelse(sp);
              return fat_iget(fdp->dev, inum, 0, fdp->inum);
            }
        }
      }
      brelse(sp);
    }
    // Find FAT entry
    curFatsect = fat_getFATEntry(&bpb, cno, &secOff);
    if (curFatsect != lastFatsect) {
      if (fp)
        brelse(fp);
  //    cprintf("before bread18 cursect = %d\n", curFatsect);
      fp = bread(fdp->dev, curFatsect);
      lastFatsect = curFatsect;
    }
    cno = *(uint*)(fp->data + secOff);
  } while (!isEOF(cno));
  brelse(fp);
  return 0;
}

static int
fat_inumtoname(struct inode *dp, int inum, char* name){
  struct fat_inode *fdp = vop_info(dp, fat_inode);
  uint cno = fdp->inum;
  uint curFatsect, lastFatsect = 0, secOff;
  uint si, s;
  struct buf *fp, *sp;
  struct BPB bpb;
  struct DIR *de;
//  cprintf("in inumtoname, inum = %d\n", fdp->inum);
  fat_readbpb(fdp->dev, &bpb);
  fp = 0;
  do {
    s = fat_getFirstSectorofCluster(&bpb, cno);
    for (si = 0; si < bpb.SecPerClus; ++si) { // Every sector
//       cprintf("secnum = %d\n", si);
 //     cprintf("before bread s + si = %d\n", s + si);
      sp = bread(fdp->dev, s + si);
  //    cprintf("si = %d\n", si);
      for (de = (struct DIR*)sp->data;
           de < (struct DIR*)(sp->data + SECTSIZE);
           ++de) {          // Every entry
  //      cprintf("FstClusHI = %d, Fstclulo = %d, inum = %d\n", de->FstClusHI, de->FstClusLO, fdp->inum);
        if (((de->FstClusHI << 16) | de->FstClusLO) == inum) {
     //     cprintf("raw name = %s\n", (char*)de->Name);
          copyshortname(name, (char*)de->Name);
          if (fp)
            brelse(fp);
          brelse(sp);
          return 0;
        }
      }
      brelse(sp);
    }
    // Find FAT entry
    curFatsect = fat_getFATEntry(&bpb, cno, &secOff);
    if (curFatsect != lastFatsect) {
      if (fp)
        brelse(fp);
   //   cprintf("before bread19 cursect = %d\n", curFatsect);
      fp = bread(fdp->dev, curFatsect);
      lastFatsect = curFatsect;
    }
    cno = *(uint*)(fp->data + secOff);
  } while (!isEOF(cno));
  brelse(fp);
  return -1;
}

int
fat_getpath(struct inode *node, char *path, int maxlen){
//  cprintf("enter fat_getpath\n");
  int ret, namelen;
  int pos = maxlen - 2;
  uint inum;
  char *ptr = path + maxlen;
  char namebuf[11];
  struct fat_inode *fin = vop_info(node, fat_inode);
  struct inode *parent;
  vop_ref_inc(node);
  while(1){
    
    if((parent = fat_dirlookup(node, "..", 0)) == 0){
      goto failed;
    }
//    cprintf("after dirlookup\n");
//    cprintf("not failed\n");
    inum = fin->inum;
    vop_ref_dec(node);
//    cprintf("after vop_ref_dec\n");
    if(node == parent){
      vop_ref_dec(node);
      break;
    }
//    cprintf("before inumtoname\n");
    node = parent;
    fin = vop_info(node, fat_inode);
//    cprintf("inum = %d\n", inum);
  //  inum = fin->inum;
    ret = fat_inumtoname(node, inum, namebuf);
//    cprintf("getpath name = %s\n", namebuf);
    if(ret != 0){
//      cprintf("ret = %d\n", ret);
      goto failed;
    }
    if((namelen = strlen(namebuf) + 1) > pos){
      return -1;
    }
    pos -= namelen;
    ptr -= namelen;
    memcpy(ptr, namebuf, namelen -1);
    ptr[namelen -1] = '/';
  }
  namelen = maxlen - pos - 2;
  ptr = memmove(path + 5, ptr, namelen);
  ptr[-1] = '/';
  ptr[-2] = ':';
  ptr[-3] = 't';
  ptr[-4] = 'a';
  ptr[-5] = 'f';
  ptr[namelen] = '\0';
  
  return 0;
failed:
  vop_ref_dec(node);
  return -1;
}

// Write a new directory entry (name, ip) into the directory dp.
int
fat_dirlink(struct inode *dp, char *name, struct inode *ip)
{
//  cprintf("enter fat_dirlink\n");
  struct fat_inode *fdp = vop_info(dp, fat_inode);
  struct fat_inode *fip = vop_info(ip, fat_inode);

  int len, dbnum, i, j;
  uchar chksum;
  struct LDIR ldbuf[20];
  struct DIR dbuf;
  ushort namebuf[FAT_DIRSIZ + 1] = {0};
  struct inode *tip;

  // Check that name is not present.
  if((tip = fat_dirlookup(dp, name, 0)) != 0){
    fat_iput(tip);
    return -1;
  }

  // Generate Name Blocks
  if (strncmp(name, ".", 1) && strncmp(name, "..", 2)) { // Long name
    fat_getshortname(name, (char*)dbuf.Name);
    while ((tip = fat_dirlookup(dp, (char*)dbuf.Name, 0)) != 0) {
      fat_iput(tip);
      fat_updatename(dbuf.Name);
    }
    chksum = fat_getChkSum(dbuf.Name);
    for (i = 0; i < FAT_DIRSIZ; ++i)
      namebuf[i] = 0xFFFF;
    len = strlen(name);
    for (i = 0; i < len; ++i)
      namebuf[i] = (ushort)name[i];
    namebuf[len] = 0;
    dbnum = (len - 1) / 13 + 1;
    for (i = 0; i < dbnum; ++i) {
      for (j = 0; j < 5; ++j)
        ldbuf[i].Name1[j] = namebuf[13 * (dbnum - i - 1) + j];
      for (j = 0; j < 6; ++j)
        ldbuf[i].Name2[j] = namebuf[13 * (dbnum - i - 1) + 5 + j];
      for (j = 0; j < 2; ++j)
        ldbuf[i].Name3[j] = namebuf[13 * (dbnum - i - 1) + 11 + j];
      ldbuf[i].Ord = dbnum - i;
      ldbuf[i].Attr = FAT_TYPE_LNAME;
      ldbuf[i].Type = 0;
      ldbuf[i].ChkSum = chksum;
      ldbuf[i].FstClusLO = 0;
    }
    
    ldbuf[0].Ord |= FAT_TYPE_LLNMASK;
  } else { // Short name
    for (i = 0; i < 11; ++i)
      dbuf.Name[i] = 0x20;
    dbuf.Name[0] = '.';
    if (name[1])
      dbuf.Name[1] = '.';
    dbnum = 0;
  }
  dbuf.Attr = fip->type;
  dbuf.NTRes = 0;
  if(fip->type & (T_DIR | T_FILE)) {
    dbuf.CrtTimeTenth = 1000*fat_getBIOSsecond()%2;
    dbuf.CrtDate = ((fat_getBIOSyear()+20)<<9)|(fat_getBIOSmonth()<<5)|fat_getBIOSday();
    dbuf.CrtTime = (fat_getBIOShour()<<11)|(fat_getBIOSminute()<<5)|(fat_getBIOSsecond()/2);
    if((dbuf.CrtTime>>11) < 8)
      dbuf.CrtDate++;
  }
  else {
    dbuf.CrtTimeTenth = 0;
  //  dbuf.CrtDate = fip->major;
  //  dbuf.CrtTime = fip->minor;
  }
  dbuf.LstAccDate = 0;
  if (fip->inum == 2) {
    dbuf.FstClusHI = dbuf.FstClusLO = 0;
  } else {
    dbuf.FstClusHI = (ushort)(fip->inum >> 16);
    dbuf.FstClusLO = (ushort)(fip->inum & 0xFFFF);
  }
  dbuf.WrtTime = dbuf.CrtTime;
  dbuf.WrtDate = dbuf.CrtDate;
  dbuf.FileSize = fip->size;

  // Update DIR entry
  uint curFatsect, lastFatsect = 0, secOff;
  uint cno, si, s;
  struct buf *fp, *sp;
  struct BPB bpb;
  struct LDIR *de;
  int last, cnt;
  uint cno0 = 0, si0 = 0, de0 = 0;

  last = 0;
  cnt = 0;
  fat_readbpb(fdp->dev, &bpb);
  fp = 0;
  cno = fdp->inum;
  do {
    s = fat_getFirstSectorofCluster(&bpb, cno);
    for (si = 0; si < bpb.SecPerClus; ++si) {   // Every sector
  //    cprintf("before bread21 cursect = %d\n", s+si);
      sp = bread(fdp->dev, s + si);
      for (de = (struct LDIR*)sp->data;
           de < (struct LDIR*)(sp->data + SECTSIZE);
           ++de) {          // Every entry
        if (fat_getDIRType(de) == FAT_TYPE_EMPTY) {
          if (dbnum == 0) {       // Only short name
            cno0 = cno;
            si0 = si;
            de0 = (uchar*)de - sp->data;
            bwrite(sp);
            brelse(sp);
            if (fp) {
              fat_updateFATs(fp);
              bwrite(fp);
              brelse(fp);
            }
            goto found;
          }
          if (last) {
            if (cnt++ == dbnum) { // Found a sequence
              bwrite(sp);
              brelse(sp);
              if (fp) {
                fat_updateFATs(fp);
                bwrite(fp);
                brelse(fp);
              }
			  fdp->size+=sizeof(struct LDIR);
              goto found;
            }
          } else {
            cnt = 1;
            cno0 = cno;
            si0 = si;
            de0 = (uchar*)de - sp->data;
			fdp->size+=sizeof(struct LDIR);
          }
        }
        last = fat_getDIRType(de) == FAT_TYPE_EMPTY;
      }
      bwrite(sp);
      brelse(sp);
    }
    // Find FAT entry
    curFatsect = fat_getFATEntry(&bpb, cno, &secOff);
    if (curFatsect != lastFatsect) {
      if (fp) {
        fat_updateFATs(fp);
        bwrite(fp);
        brelse(fp);
      }
  //    cprintf("before bread22 cursect = %d\n", curFatsect);
      fp = bread(fdp->dev, curFatsect);
      lastFatsect = curFatsect;
    }
    cno = *(uint*)(fp->data + secOff);
    if (isEOF(cno))
      cno = *(uint*)(fp->data + secOff) = fat_calloc(fdp->dev);
  } while (1);

found:
  fdp->size+=sizeof(struct DIR);
  fp = 0;
  cno = cno0;
  do {
    s = fat_getFirstSectorofCluster(&bpb, cno);
    for (si = si0; si < bpb.SecPerClus; ++si) { // Every sector
  //    cprintf("before bread24 cursect = %d\n", s+si);
      sp = bread(fdp->dev, s + si);
      for (de = (struct LDIR*)(sp->data + de0), i = 0;
           de < (struct LDIR*)(sp->data + SECTSIZE) && i <= dbnum;
           ++de, ++i) {  // Every entry
        if (i == dbnum) {
          memmove(de, &dbuf, sizeof(dbuf));
          bwrite(sp);
          brelse(sp);
          if (fp)
            brelse(fp);
          return 0;
        } else {
          memmove(de, &ldbuf[i], sizeof(ldbuf[0]));
        }
      }
      bwrite(sp);
      brelse(sp);
      de0 = 0;
    }
    // Find FAT entry
    curFatsect = fat_getFATEntry(&bpb, cno, &secOff);
    if (curFatsect != lastFatsect) {
      if (fp)
        brelse(fp);
  //    cprintf("before bread25 cursect = %d\n", curFatsect);
      fp = bread(fdp->dev, curFatsect);
      lastFatsect = curFatsect;
    }
    cno = *(uint*)(fp->data + secOff);
    si0 = 0;
  } while (!isEOF(cno));
  panic("dirlink");
}

// Paths.

// Copy the next path element from path into name.
// Return a pointer to the element following the copied one.
// The returned path has no leading slashes,
// so the caller can check *path=='\0' to see if the name is the last one.
// If no name to remove, return 0.
//
// Examples:
//   fat_skipelem("a/bb/c", name) = "bb/c", setting name = "a"
//   fat_skipelem("///a//bb", name) = "bb", setting name = "a"
//   fat_skipelem("a", name) = "", setting name = "a"
//   fat_skipelem("", name) = skipelem("////", name) = 0
//
static char*
fat_skipelem(char *path, char *name)
{
  char *s;
  int len;

  while(*path == '/')
    path++;
  if(*path == 0)
    return 0;
  s = path;
  while(*path != '/' && *path != 0)
    path++;
  len = path - s;
  if(len >= FAT_DIRSIZ)
    memmove(name, s, FAT_DIRSIZ);
  else {
    memmove(name, s, len);
    name[len] = 0;
  }
  while(*path == '/')
    path++;
  return path;
}

// Look up and return the inode for a path name.
// If parent != 0, return the inode for the parent and copy the final
// path element into name, which must have room for DIRSIZ bytes.
static struct inode*
fat_namex(struct inode *ip, char *path, int nameiparent, char *name)// modified 12.25
{
  struct inode *next;// modified 12.25
  struct fat_inode *fip;// modified 12.25
  
  // if(*path == '/')
  //   //ip = iget(ROOTDEV, 2, 0);
  // //ip = iget(FAT_ROOTDEV, 2, 0);
  // // to set FAT_ROOTDEV as 2..............................................
  // ip = fat_iget(2, 2, 0);
  // else
  //   ip = fat_idup(proc->cwd);
  
  if (*path == '.'){
    struct inode *cnode = proc->cwd;
    struct fat_inode *fnode = vop_info(cnode, fat_inode);
    if(fnode->inum == 2){
      ++path;
    }
  }
  // above: removed 12.25

  while((path = fat_skipelem(path, name)) != 0){
    fat_ilock(ip);
    fip = vop_info(ip, fat_inode);
 //   cprintf("namex1: inum = %d\n", fip->inum);
    if(fip->type != T_DIR){
      fat_iunlockput(ip);
      return 0;
    }
    if(nameiparent && *path == '\0'){
      // Stop one level early.
      fat_iunlock(ip);
      return ip;
    }
    if((next = fat_dirlookup(ip, name, 0)) == 0){
      fat_iunlockput(ip);
      return 0;
    }
    fat_iunlockput(ip);
    ip = next;
  }
  if(nameiparent){
    fat_iput(ip);
 //   cprintf("namex4: inum = %d\n", fip->inum);
    return 0;
  }
 // cprintf("namex5: inum = %d\n", fip->inum);
  return ip;
}

struct inode*
fat_namei(struct inode *node, char *path)// modified 12.25
{
//  cprintf("enter fatnamei\n");
  char name[FAT_DIRSIZ];
  return fat_namex(node, path, 0, name);// modified 12.25
}

struct inode*
fat_nameiparent(struct inode *node, char *path, char *name)// modified 12.25
{
  return fat_namex(node, path, 1, name);// modified 12.25
}


// functions added (begin)

int
fat_isdirempty(struct inode *dp)
{
  int off;
  struct DIR de;
  struct fat_inode *fdp = vop_info(dp, fat_inode);
  
  for(off=2*sizeof(de); off<fdp->size; off+=sizeof(de)){
    if(vop_read(dp, (char*)&de, off, sizeof(de)) != sizeof(de))
      panic("isdirempty: readi");
    if(de.Name[0] != 0xE5 && de.Name[0] != 0x00)
      return 0;
  }
  return 1;
}

// Increment reference count for ip.
// Returns ip to enable ip = idup(ip1) idiom.
struct inode*
fat_link_inc(struct inode *ip)
{
  struct fat_inode *fip = vop_info(ip, fat_inode);
  acquire(&icache.lock);
  fip->nlink++;
  release(&icache.lock);
  return ip;
}

// Increment reference count for ip.
// Returns ip to enable ip = idup(ip1) idiom.
struct inode*
fat_link_dec(struct inode *ip)
{
  struct fat_inode *fip = vop_info(ip, fat_inode);
  acquire(&icache.lock);
  fip->nlink--;
  release(&icache.lock);
  return ip;
}

struct inode*
fat_create_inode(struct inode *dirnode, short type, short major, short minor, char* name) {
//  cprintf("enter fat_create_inode name = %s\n", name);
  struct inode *ip;
  struct fat_inode *dp = vop_info(dirnode, fat_inode);
  
  if((ip = fat_iget(dp->dev, fat_calloc(dp->dev), type, dp->inum)) == 0)
    goto bad;

  struct fat_inode *fip = vop_info(ip, fat_inode);
//  fip->type = type;
//  vop_ilock(ip);
  fip->major = major;
  fip->minor = minor;
  fip->size = 0;
  fip->nlink = 1;
//  vop_iupdate(ip);
  if(type == T_DIR){  // Create . and .. entries.
    if(fat_dirlink(ip, ".", ip) < 0 || fat_dirlink(ip, "..", dirnode) < 0)
      panic("create dots");
  }

  if(fat_dirlink(dirnode, name, ip) < 0)
    panic("create: dirlink");

  fat_ilock(ip);
  fat_iupdate(ip);
//  cprintf("create inum = %d\n", fip->inum);
  if(dp->inum != 2)
    fat_iupdate(dirnode);
//  cprintf("ipinum = %d\n", fip->inum);
  return ip;

bad:
  return 0;  
}

static int
fat_opendir(struct inode *node, int open_flags) {
  if(open_flags != O_RDONLY){//modified 12.27
    return -1;
  }
  return 0;
}

static int
fat_openfile(struct inode *node, int open_flags) {
  return 0;
}

static short
fat_gettype(struct inode *node){
  struct fat_inode *fnode = vop_info(node, fat_inode);
  return fnode->type;
}

static uint
fat_getdev(struct inode *node){
  struct fat_inode *fnode = vop_info(node, fat_inode);
  return fnode->dev;
}

static short
fat_getnlink(struct inode *node){
  struct fat_inode *fnode = vop_info(node, fat_inode);
  return fnode->nlink;
}

static short
sfs_getmajor(struct inode *node){
  struct fat_inode *fnode = vop_info(node, fat_inode);
  return fnode->major;
}

static short
sfs_getminor(struct inode *node){
  struct fat_inode *fnode = vop_info(node, fat_inode);
  return fnode->minor;
}

int
fat_unlink(struct inode *dp, char *name){
//  cprintf("enter fat_unlink\n");
  struct inode *ip;

  vop_ilock(dp);
  // Cannot unlink "." or "..".
  if(fat_namecmp(name, ".") == 0 || fat_namecmp(name, "..") == 0)
    goto bad;

  if((ip = vop_dirlookup(dp, name, 0)) == 0)//modiefied 12.27
    goto bad;
  vop_ilock(ip);
  struct fat_inode *fip = vop_info(ip, fat_inode);
  struct fat_inode *fdp = vop_info(dp, fat_inode);
  fdp->size -= (fip->size + sizeof(struct DIR));
  if(fip->nlink < 1)
    panic("unlink: nlink < 1");
 // cprintf("rm: %d, %d, %d\n", fip->inum, fip->type, fip->size);
  if(fip->type == T_DIR && !vop_isdirempty(ip)){
    vop_iunlockput(ip);
    goto bad;
  }
  vop_iunlockput(dp);
  vop_link_dec(ip);
  vop_iupdate(ip);
  if(fdp->inum != 2)
    vop_iupdate(dp);
  vop_iunlockput(ip);
  return 0;
  
bad:
  vop_iunlockput(dp);
  return -1;
}

// functions added (end)


// The fat specific DIR operations correspond to the abstract operations on a inode.
static const struct inode_ops fat_node_dirops = {
    .vop_magic                      = VOP_MAGIC,
    .vop_read                       = fat_readi,
    .vop_write                      = fat_writei,
    .vop_fstat                      = fat_stati,
    .vop_iupdate                    = fat_iupdate,
    .vop_ref_inc                    = fat_idup,
    .vop_ref_dec                    = fat_iput,
    .vop_namei                      = fat_namei,
    .vop_nameiparent                = fat_nameiparent,
    .vop_dirlink                    = fat_dirlink,
    .vop_unlink                     = fat_unlink,
    .vop_dirlookup                  = fat_dirlookup,
    .vop_ilock                      = fat_ilock,
    .vop_iunlock                    = fat_iunlock,
    .vop_iunlockput                 = fat_iunlockput,
    //.vop_ialloc                     = fat_ialloc,  //这个函数在fat32里没有用到。。。
    .vop_isdirempty                 = fat_isdirempty,
    .vop_link_inc                   = fat_link_inc,
    .vop_link_dec                   = fat_link_dec,
    .vop_create_inode               = fat_create_inode,
    .vop_open                       = fat_opendir,
    .vop_gettype                    = fat_gettype,
    .vop_getdev                     = fat_getdev,
    .vop_getnlink                   = fat_getnlink,
    .vop_getpath                    = fat_getpath,
}; 

// The fatfs specific FILE operations correspond to the abstract operations on a inode.
static const struct inode_ops fat_node_fileops = {
    .vop_magic                      = VOP_MAGIC,
    .vop_read                       = fat_readi,
    .vop_write                      = fat_writei,
    .vop_fstat                      = fat_stati,
    .vop_iupdate                    = fat_iupdate,
    .vop_ref_inc                    = fat_idup,
    .vop_ref_dec                    = fat_iput,
    .vop_ilock                      = fat_ilock,
    .vop_iunlock                    = fat_iunlock,
    .vop_iunlockput                 = fat_iunlockput,
    .vop_link_inc                   = fat_link_inc,
    .vop_link_dec                   = fat_link_dec,
    .vop_open                       = fat_openfile,
    .vop_gettype                    = fat_gettype,
    .vop_getdev                     = fat_getdev,
    .vop_getnlink                   = fat_getnlink,
    .vop_getpath                    = fat_getpath,
    .vop_getmajor                   = sfs_getmajor,
    .vop_getminor                   = sfs_getminor,
};
