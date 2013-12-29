#define T_DIR  1   // Directory
#define T_FILE 2   // File
#define T_DEV  3   // Device
#define FAT_TYPE_FILE    0x20 // added 12.27
#define FAT_TYPE_DIR     0x10 // added 12.27
#define FAT_TYPE_DEV     0x04
#define FAT_TYPE_VOLLBL  0x08   // Volume Label// added 12.27
#define FAT_TYPE_LNAME   0x0F   // Long File Name// added 12.27
#define FAT_TYPE_EMPTY   0x5A   // Empty Directory Entry// added 12.27
#define FAT_TYPE_ERROR   0xFF   // Erro Directory Entry// added 12.27
#define FAT_TYPE_LNMASK  0x3F   // Long File Name Mask// added 12.27
#define FAT_TYPE_LLNMASK 0x40   // Last Long File Name Mask// added 12.27

struct stat {
  short type;  // Type of file
  int dev;     // File system's disk device
  uint ino;    // Inode number
  short nlink; // Number of links to file
  uint size;   // Size of file in bytes
};
