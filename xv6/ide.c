// Simple PIO-based (non-DMA) IDE driver code.

#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "x86.h"
#include "traps.h"
#include "spinlock.h"
#include "buf.h"

#define ISA_DATA                0x00
#define ISA_ERROR               0x01
#define ISA_PRECOMP             0x01
#define ISA_CTRL                0x02
#define ISA_SECCNT              0x02
#define ISA_SECTOR              0x03
#define ISA_CYL_LO              0x04
#define ISA_CYL_HI              0x05
#define ISA_SDH                 0x06
#define ISA_COMMAND             0x07
#define ISA_STATUS              0x07

#define IDE_BSY                 0x80
#define IDE_DRDY                0x40
#define IDE_DF                  0x20
#define IDE_ERR                 0x01

#define IDE_CMD_READ            0x20
#define IDE_CMD_WRITE           0x30

#define IO_BASE0                0x1F0
#define IO_BASE1                0x170
#define IO_CTRL0                0x3F4
#define IO_CTRL1                0x374

// idequeue points to the buf now being read/written to the disk.
// idequeue->qnext points to the next buf to be processed.
// You must hold idelock while manipulating queue.

static struct {
    const unsigned short base;  // I/O Base
    const unsigned short ctrl;  // Control Base
} channels[2] = {
    {IO_BASE0, IO_CTRL0},
    {IO_BASE1, IO_CTRL1},
};

#define IO_BASE(ideno)          (channels[(ideno) >> 1].base)
#define IO_CTRL(ideno)          (channels[(ideno) >> 1].ctrl)

static struct spinlock idelock;
static struct buf *idequeue;

static int havedisk1;
static int havedisk2;
static void idestart(struct buf*);

// Wait for IDE disk to become ready.
static int
idewait(ushort iobase, int checkerr)
{
  int r;
  r = inb(iobase + ISA_STATUS);
//  cprintf("OUT iobase = %d, r = %d\n", iobase, r);
  while((r = inb(iobase + ISA_STATUS)) & IDE_BSY) {
    //cprintf("iobase = %d, r = %d\n", iobase, r);
  }
//  cprintf("after iobase = %d, r = %d\n", iobase, r);
  if(checkerr && (r & (IDE_DF|IDE_ERR)) != 0)
    return -1;
  return 0;
}

void
ideinit(void)
{
  int i;

  initlock(&idelock, "ide");
  picenable(IRQ_IDE0);
  picenable(IRQ_IDE1);
  ioapicenable(IRQ_IDE0, ncpu - 1);
  ioapicenable(IRQ_IDE1, ncpu - 1);

  idewait(IO_BASE0, 0);
  // Check if disk 1 is present
  outb(IO_BASE0 + ISA_SDH, 0xe0 | (1<<4));
  for(i=0; i<1000; i++){
    if(inb(IO_BASE0 + ISA_COMMAND) != 0){
      havedisk1 = 1;
      break;
    }
  }
  
  idewait(IO_BASE1, 0);
  outb(IO_BASE1 + ISA_SDH, 0xe0 | (0<<4));
  for(i=0; i<1000; i++){
    if(inb(IO_BASE1 + ISA_COMMAND) != 0){
      havedisk2 = 1;
      break;
    }
  }
  // Switch back to disk 0.
  idewait(IO_BASE0, 0);
  outb(IO_BASE0 + ISA_SDH, 0xe0 | (0<<4));
}

// Start the request for b.  Caller must hold idelock.
static void
idestart(struct buf *b)
{
  if(b == 0)
    panic("idestart");
  
  ushort iobase = IO_BASE(b->dev);
  ushort ioctrl = IO_CTRL(b->dev);
  
  idewait(iobase, 0);
//  cprintf("before idestart\n");
  outb(ioctrl + ISA_CTRL, 0);  // generate interrupt
  outb(iobase + ISA_SECCNT, 1);  // number of sectors
  outb(iobase + ISA_SECTOR, b->sector & 0xff);
  outb(iobase + ISA_CYL_LO, (b->sector >> 8) & 0xff);
  outb(iobase + ISA_CYL_HI, (b->sector >> 16) & 0xff);
  outb(iobase + ISA_SDH, 0xe0 | ((b->dev&1)<<4) | ((b->sector>>24)&0x0f));
//  cprintf("middle idestart\n");
  if(b->flags & B_DIRTY){
    outb(iobase + ISA_COMMAND, IDE_CMD_WRITE);
    outsl(iobase, b->data, 512/4);
//    cprintf("after idewrite\n");
  } else {
    outb(iobase + ISA_COMMAND, IDE_CMD_READ);
//    cprintf("after ideread\n");
  }
}

// Interrupt handler.
void
ideintr(void)
{
  struct buf *b;

  // First queued buffer is the active request.
  acquire(&idelock);
  if((b = idequeue) == 0){
    release(&idelock);
    // cprintf("spurious IDE interrupt\n");
    return;
  }
  idequeue = b->qnext;
  
  ushort iobase = IO_BASE(b->dev);
  // Read data if needed.
  if(!(b->flags & B_DIRTY) && idewait(iobase, 1) >= 0)
    insl(iobase, b->data, 512/4);
  
  // Wake process waiting for this buf.
  b->flags |= B_VALID;
  b->flags &= ~B_DIRTY;
  wakeup(b);
  
  // Start disk on next buf in queue.
  if(idequeue != 0)
    idestart(idequeue);

  release(&idelock);
}

//PAGEBREAK!
// Sync buf with disk. 
// If B_DIRTY is set, write buf to disk, clear B_DIRTY, set B_VALID.
// Else if B_VALID is not set, read buf from disk, set B_VALID.
void
iderw(struct buf *b)
{
  struct buf **pp;
  
  if(!(b->flags & B_BUSY))
    panic("iderw: buf not busy");
  if((b->flags & (B_VALID|B_DIRTY)) == B_VALID)
    panic("iderw: nothing to do");
  if(b->dev != 0 && !havedisk1)
    panic("iderw: ide disk 1 not present");
  if(b->dev != 0 && !havedisk2)
    panic("iderw: ide disk 2 not present");
  acquire(&idelock);  //DOC:acquire-lock
  // Append b to idequeue.
  b->qnext = 0;
  for(pp=&idequeue; *pp; pp=&(*pp)->qnext)  //DOC:insert-queue
    ;
  *pp = b;
  
  // Start disk if necessary.
  if(idequeue == b)
    idestart(b);
//  cprintf("after idestart iderw dev = %d, flags=%d, data=%d\n", b->dev, b->flags, b->data[0]);
  // Wait for request to finish.
  while((b->flags & (B_VALID|B_DIRTY)) != B_VALID){
    sleep(b, &idelock);
//    cprintf("b->flag=%d\n",b->flags);
  }
//  cprintf("after sleep iderw");
  release(&idelock);
}
