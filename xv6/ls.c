#include "types.h"
#include "stat.h"
#include "user.h"
#include "fs.h"
#include "sfs_inode.h"
#include "fat_inode.h"

uchar getDay(ushort date)
{
    return date & 0x1f;
}

uchar getMonth(ushort date)
{
    return (date >> 5) & 0x0F;
}

ushort getYear(ushort date)
{
    return 1980 + (date >> 9);
}

uchar getSecond(ushort time, uchar tenth)
{
    return 2 * (time & 0x1f) + tenth / 100;
}

uchar getMinute(ushort time)
{
    return (time >> 5) & 0x3f;
}

uchar getHour(ushort time)
{
    return time >> 11;
}
// get DIR type.
DIR_TYPE getDIRType(struct LDIR* ldir)
{
    if (ldir->Ord == 0xE5 || ldir->Ord == 0x00)
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

uchar
getChkSum(uchar* pFcbName)
{
    ushort FcbNameLen;
    uchar Sum = 0;
    for (FcbNameLen=11; FcbNameLen!=0; --FcbNameLen) {
        Sum = ((Sum & 1) ? 0x80 : 0) + (Sum >> 1) + *pFcbName++;
    }
    return (Sum);
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

void
copylongname(char *dst, char *src)
{
  while (*src && *src != 0xFF)
    *dst++ = *src++;
  *dst++ = 0;
}

char*
fmtname(char *path)
{
  static char buf[DIRSIZ+1];
  char *p;
  
  // Find first character after last slash.
  for(p=path+strlen(path); p >= path && *p != '/'; p--)
    ;
  p++;
  
  // Return blank-padded name.
  if(strlen(p) >= DIRSIZ)
    return p;
  memmove(buf, p, strlen(p));
  memset(buf+strlen(p), ' ', DIRSIZ-strlen(p));
  return buf;
}

void sfs_ls(char *path, int fd, struct stat st)
{
  char buf[512], *p;
  struct sfs_dirent de;
  switch(st.type){
  case T_FILE:
    printf(1, "%s %d %d %d\n", fmtname(path), st.type, st.ino, st.size);
    break;
  
  case T_DIR:
    if(strlen(path) + 1 + DIRSIZ + 1 > sizeof buf){
      printf(1, "ls: path too long\n");
      break;
    }
    strcpy(buf, path);
    p = buf+strlen(buf);
    *p++ = '/';
    while(read(fd, &de, sizeof(de)) == sizeof(de)){
      if(de.inum == 0)
        continue;
      memmove(p, de.name, DIRSIZ);
      p[DIRSIZ] = 0;
      if(stat(buf, &st) < 0){
        printf(1, "ls: cannot stat %s\n", buf);
        continue;
      }
      printf(1, "%s %d %d %d\n", fmtname(buf), st.type, st.ino, st.size);
    }
    break;
  }
}

void fat_ls(char *path, int fd, struct stat st)
{
  char buf[512], *p;
  struct LDIR de;
  struct DIR* dir;
  char namebuf[FAT_DIRSIZ + 1] = {0};
  uchar chksum = 0;
  int ord = 0;
  int nbp = 0, i;
  uchar hour, minute, second, month, day;
  ushort year;
  switch(st.type){
  case T_FILE:
    printf(1, "%s %d %d %d\n", fmtname(path), st.type, st.ino, st.size);
    break;
  
  case T_DIR:
    if(strlen(path) + 1 + DIRSIZ + 1 > sizeof buf){
      printf(1, "ls: path too long\n");
      break;
    }
    strcpy(buf, path);
    p = buf+strlen(buf);
    *p++ = '/';
//    printf(1, "fd = %d\n",fd);
    while(read(fd, &de, sizeof(de)) == sizeof(de)){
//      printf(1, "enter while read\n");
      switch (getDIRType(&de)) {
        case FAT_TYPE_VOLLBL:
        case FAT_TYPE_EMPTY:
          break;
        case FAT_TYPE_ERROR:
          printf(1, "ls: wrong DIR entry\n");
          break;
        case FAT_TYPE_LNAME:
          if (de.Ord & FAT_TYPE_LLNMASK) { // Last Entry
            nbp = DIRSIZ - 1;
            chksum = de.ChkSum;
            ord = de.Ord - FAT_TYPE_LLNMASK;
          } else if (chksum != de.ChkSum
                     || --ord != de.Ord)
            printf(1, "ls: long filename wrong\n");
          for (i = 0; i != 2; ++i)
            namebuf[nbp - 2 + i] = de.Name3[i] > 255 ? '_' : (char)de.Name3[i];
          for (i = 0; i != 6; ++i)
            namebuf[nbp - 8 + i] = de.Name2[i] > 255 ? '_' : (char)de.Name2[i];
          for (i = 0; i != 5; ++i)
            namebuf[nbp - 13 + i] = de.Name1[i] > 255 ? '_' : (char)de.Name1[i];
          nbp -= 13;
          break;

        default:
          dir = (struct DIR*)&de;
          if (getChkSum((uchar*)dir->Name) == chksum) // long name
            copylongname(p, namebuf + nbp);
          else
            copyshortname(p, (char*)dir->Name);
          p[DIRSIZ] = 0;
          if(stat(buf, &st) < 0){
            printf(1, "ls: cannot stat %s\n", buf);
            break;
          }
          hour=getHour(dir->CrtTime);
          minute=getMinute(dir->CrtTime);
          second=getSecond(dir->CrtTime,dir->CrtTimeTenth);
          year=getYear(dir->CrtDate);
          month=getMonth(dir->CrtDate);
          day=getDay(dir->CrtDate);
          printf(1, "%s %d %d %d %d-%d-%d %d:%d:%d\n", fmtname(buf), st.type, st.ino,
                 st.size,year,month,day,hour,minute,second);
      }
    }
    break;
  }
}

void
ls(char *path)
{
  int fd; 
  struct stat st;
  if((fd = open(path, 0)) < 0){
    printf(2, "ls: cannot open %s\n", path);
    return;
  }
  if(fstat(fd, &st) < 0){
    printf(2, "ls: cannot stat %s\n", path);
    close(fd);
    return;
  }
  switch(st.fstype){
  case SFS_INODE:
    sfs_ls(path, fd, st);
    break;
  case FAT_INODE:
    fat_ls(path, fd, st);
    break;
  }
//  printf(2, "stfstype = %d\n", st.fstype);
//  printf(1, "before closefd\n");
  close(fd);
}

int
main(int argc, char *argv[])
{
  int i;

  if(argc < 2){
    ls(".");
    exit();
  }
  for(i=1; i<argc; i++)
    ls(argv[i]);
  exit();
}
