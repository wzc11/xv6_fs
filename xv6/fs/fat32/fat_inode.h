#ifndef FAT_INODE_H
#define FAT_INODE_H

// On-disk file system format. 
// Both the kernel and user programs use this header file.

// Block 0 is for BPB(Bios Parameter Block)
// Block 1 & 2 are for FAT32(File Allocation Table)

#define SECTSIZE 512  // sector size

// in-memory file system structure
struct fat_inode {
  uint dev;           // Device number
  uint inum;          // Inode number
  int ref;            // Reference count
  int flags;          // I_BUSY, I_VALID

  short type;         // copy of disk inode
  short major;
  short minor;
  short nlink;
  uint size;
  uint dircluster;
};

// On-disk file system format. 
// Both the kernel and user programs use this header file.
// BIOS Parameter Block
struct BPB {
    uchar   jmpBoot[3];     // Jump Code
    uchar   OEMName[8];     // Should be "MSWIN4.1"
    ushort  BytsPerSec;     // Bytes per sector
    uchar   SecPerClus;     // Clusters per sector
    ushort  ResvdSecCnt;    // Reserved sector count
    uchar   NumFATs;        // count of FAT(usually 2)
    ushort  RootEntCnt;     // 0 for FAT32
    ushort  TotSec16;       // count of sectors in 16bit form(0 for FAT32)
    uchar   Media;          // 0xF8
    ushort  FATSz16;        // size of FAT(0 for FAT32)
    ushort  SecPerTrk;      // Sectors per track
    ushort  NumHeads;       // count of heads
    uint    HiddSec;        // count of hidden sectors
    uint    TotSec32;       // count of sectors in 32bit form
    uint    FATSz32;        // size of FAT(how many sectors)
    ushort  ExtFlags;       // 0
    ushort  FSVer;          // File system version(0)
    uint    RootClus;       // Cluster number of root(usually 2)
    ushort  FSInfo;         // Sector number of FSInfo(usally 1)
    ushort  BkBootSec;      // Sector number of boot sector's backup
    uchar   Reserved[12];   // 0
    uchar   DrvNum;         // Drive number(0x80 for hard drive)
    uchar   Reserved1;      // 0
    uchar   BootSig;        // Boot signature(0x29)
    uint    VolID;          // Volume ID(a random number)
    uchar   VolLab[11];     // Volume label(FAT32 has A FILE to describe the volume label)
    uchar   FilSysType[8];  // File system type("FAT32   ")
}__attribute__ ((packed));

// File System Info
struct FSI {
    uint    LeadSig;
    uchar   Reserved1[480];
    uint    StructSig;
    uint    Free_Count;
    uint    Nxt_Free;
    uchar   Reserved2[12];
    uint    TrailSig;
}__attribute__ ((packed));

#define FAT_DIRSIZ 260

// DIR Descriptor
struct DIR {
    uchar   Name[11];       // Short name
    uchar   Attr;           // Attribute of a FILE
    uchar   NTRes;          // 0
    uchar   CrtTimeTenth;   // Last 2000ms of created time
    ushort  CrtTime;        // Created time(of 2s)
    ushort  CrtDate;        // Created date
    ushort  LstAccDate;     // Last accessed date
    ushort  FstClusHI;      // High 16 bits of first cluster number
    ushort  WrtTime;        // Write time
    ushort  WrtDate;        // Write date
    ushort  FstClusLO;      // Low 16 bits of first cluster number
    uint    FileSize;       // File size
}__attribute__ ((packed));

// Long name descriptor
struct LDIR {
    uchar   Ord;        // Order of a LDIR(last one if masked T_LLNMASK)
    ushort  Name1[5];   // Char 1-5
    uchar   Attr;       // Should be T_LNMASK
    uchar   Type;       // 0
    uchar   ChkSum;     // Check Sum of its short name
    ushort  Name2[6];   // Char 6-11
    ushort  FstClusLO;  // 0
    ushort  Name3[2];   // Char 12-13
}__attribute__ ((packed));

// If it's the last entry of this FILE in FAT
#define isEOF(n)       ((n) >= 0x0FFFFFF8)

#define LAST_FAT_ENTRY 0x0FFFFFFF

// Descriptors per sector
#define DPS            (SECTSIZE / sizeof(struct DIR))

struct inode*
fat_iget(uint dev, uint inum, short type, uint dircluster);

#endif