//
// File-system system calls.
// Mostly argument checking, since we don't trust
// user code, and calls into file.c and fs.c.
//

#include "types.h"
#include "defs.h"
#include "param.h"
#include "stat.h"
#include "mmu.h"
#include "proc.h"
#include "fs.h"
#include "file.h"
#include "fcntl.h"
#include "inode.h"
#include "vfs.h"

// Fetch the nth word-sized system call argument as a file descriptor
// and return both the descriptor and the corresponding struct file.
static int
argfd(int n, int *pfd, struct file **pf)
{
  int fd;
  struct file *f;

  if(argint(n, &fd) < 0)
    return -1;
  if(fd < 0 || fd >= NOFILE || (f=proc->ofile[fd]) == 0)
    return -1;
  if(pfd)
    *pfd = fd;
  if(pf)
    *pf = f;
  return 0;
}

// Allocate a file descriptor for the given file.
// Takes over file reference from caller on success.
static int
fdalloc(struct file *f)
{
  int fd;

  for(fd = 0; fd < NOFILE; fd++){
    if(proc->ofile[fd] == 0){
      proc->ofile[fd] = f;
      return fd;
    }
  }
  return -1;
}

int
sys_dup(void)
{
  struct file *f;
  int fd;
  
  if(argfd(0, 0, &f) < 0)
    return -1;
  if((fd=fdalloc(f)) < 0)
    return -1;
  filedup(f);
  return fd;
}

int
sys_read(void)
{
  struct file *f;
  int n;
  char *p;

  if(argfd(0, 0, &f) < 0 || argint(2, &n) < 0 || argptr(1, &p, n) < 0)
    return -1;
  return fileread(f, p, n);
}

int
sys_write(void)
{
  struct file *f;
  int n;
  char *p;

  if(argfd(0, 0, &f) < 0 || argint(2, &n) < 0 || argptr(1, &p, n) < 0)
    return -1;
  return filewrite(f, p, n);
}

int
sys_close(void)
{
  int fd;
  struct file *f;
  
  if(argfd(0, &fd, &f) < 0)
    return -1;
  proc->ofile[fd] = 0;
  fileclose(f);
  return 0;
}

int
sys_fstat(void)
{
  struct file *f;
  struct stat *st;
  
  if(argfd(0, 0, &f) < 0 || argptr(1, (void*)&st, sizeof(*st)) < 0)
    return -1;
  return filestat(f, st);
}

// Create the path new as a link to the same inode as old.
int
sys_link(void)
{
  char name[DIRSIZ], *new, *old;
  struct inode *dp, *ip;

  if(argstr(0, &old) < 0 || argstr(1, &new) < 0)
    return -1;
  if((ip = vfs_lookup(old)) == 0)
    return -1;

  begin_trans();

  vop_ilock(ip);
  if(vop_gettype(ip) == T_DIR){
    vop_iunlockput(ip);
    commit_trans();
    return -1;
  }

  vop_link_inc(ip);
  vop_iupdate(ip);
  vop_iunlock(ip);

  if((dp = vfs_lookup_parent(new, name)) == 0)
    goto bad;
  vop_ilock(dp);
  if(vop_getdev(dp) != vop_getdev(ip) || vop_dirlink(dp, name, ip) < 0){
    vop_iunlockput(dp);
    goto bad;
  }
  vop_iunlockput(dp);
  vop_ref_dec(ip);

  commit_trans();

  return 0;

bad:
  vop_ilock(ip);
  vop_link_dec(ip);
  vop_iupdate(ip);
  vop_iunlockput(ip);
  commit_trans();
  return -1;
}

//PAGEBREAK!
int
sys_unlink(void)
{
  struct inode *dp;
  char name[DIRSIZ], *path;

  if(argstr(0, &path) < 0)
    return -1;
  if((dp = vfs_lookup_parent(path, name)) == 0)
    return -1;

  begin_trans();

  if(vop_unlink(dp, name) != 0)
    goto bad;

  commit_trans();

  return 0;

bad:
  commit_trans();
  return -1;
}

static struct inode*
create(char *path, short type, short major, short minor)
{
  uint off;
  struct inode *ip, *dp;
  char name[DIRSIZ];
  
  cprintf("enter create, type=%d\n", type);
  if((dp = vfs_lookup_parent(path, name)) == 0)
    return 0;
  vop_ilock(dp);

  if((ip = vop_dirlookup(dp, name, &off)) != 0){
    vop_iunlockput(dp);
    vop_ilock(ip);
    if(type == T_FILE && vop_gettype(ip) == T_FILE)
      return ip;
    vop_iunlockput(ip);
    return 0;
  }

  cprintf("before create inode, dpinum=%d\n", vop_gettype(dp));

  if((ip = vop_create_inode(dp, type, major, minor)) == 0)
    panic("create: ialloc");

  cprintf("successful create ip\n");

  if(type == T_DIR){  // Create . and .. entries.
    vop_link_inc(dp);  // for ".."
    vop_iupdate(dp);
    // No ip->nlink++ for ".": avoid cyclic ref count.
    if(vop_dirlink(ip, ".", ip) < 0 || vop_dirlink(ip, "..", dp) < 0)
      panic("create dots");
  }

  if(vop_dirlink(dp, name, ip) < 0)
    panic("create: dirlink");

  vop_iunlockput(dp);

  return ip;
}

int
sys_open(void)
{
  char *path;
  int fd, omode;
  struct file *f;
  struct inode *ip;

  if(argstr(0, &path) < 0 || argint(1, &omode) < 0)
    return -1;
  if(omode & O_CREATE){
    begin_trans();
    ip = create(path, T_FILE, 0, 0);
    commit_trans();
    if(ip == 0)
      return -1;
  } else {
    if((ip = vfs_lookup(path)) == 0)
      return -1;
    vop_ilock(ip);
    if(vop_open(ip, omode) != 0){
      vop_iunlockput(ip);
      return -1;
    }
  }

  if((f = filealloc()) == 0 || (fd = fdalloc(f)) < 0){
    if(f)
      fileclose(f);
    vop_iunlockput(ip);
    return -1;
  }
  vop_iunlock(ip);

  f->type = FD_INODE;
  f->ip = ip;
  f->off = 0;
  f->readable = !(omode & O_WRONLY);
  f->writable = (omode & O_WRONLY) || (omode & O_RDWR);
  return fd;
}

int
sys_mkdir(void)
{
  char *path;
  struct inode *ip;

  begin_trans();
  if(argstr(0, &path) < 0 || (ip = create(path, T_DIR, 0, 0)) == 0){
    commit_trans();
    return -1;
  }
  vop_iunlockput(ip);
  commit_trans();
  return 0;
}

int
sys_mknod(void)
{
  cprintf("enter sys_mknod\n");
  struct inode *ip;
  char *path;
  int len;
  int major, minor;
  
  begin_trans();
  if((len=argstr(0, &path)) < 0 ||
     argint(1, &major) < 0 ||
     argint(2, &minor) < 0 ||
     (ip = create(path, T_DEV, major, minor)) == 0){
    commit_trans();
    return -1;
  }
  vop_iunlockput(ip);
  commit_trans();
  return 0;
}

int
sys_chdir(void)
{
  char *path;
  struct inode *ip;

  if(argstr(0, &path) < 0 || (ip = vfs_lookup(path)) == 0)
    return -1;
  vop_ilock(ip);
  if(vop_gettype(ip) != T_DIR){
    vop_iunlockput(ip);
    return -1;
  }
  vop_iunlock(ip);
  vop_ref_dec(proc->cwd);
  proc->cwd = ip;
  return 0;
}

int
sys_exec(void)
{
  char *path, *argv[MAXARG];
  int i;
  uint uargv, uarg;

  if(argstr(0, &path) < 0 || argint(1, (int*)&uargv) < 0){
    return -1;
  }
  memset(argv, 0, sizeof(argv));
  for(i=0;; i++){
    if(i >= NELEM(argv))
      return -1;
    if(fetchint(uargv+4*i, (int*)&uarg) < 0)
      return -1;
    if(uarg == 0){
      argv[i] = 0;
      break;
    }
    if(fetchstr(uarg, &argv[i]) < 0)
      return -1;
  }
  return exec(path, argv);
}

int
sys_pipe(void)
{
  int *fd;
  struct file *rf, *wf;
  int fd0, fd1;

  if(argptr(0, (void*)&fd, 2*sizeof(fd[0])) < 0)
    return -1;
  if(pipealloc(&rf, &wf) < 0)
    return -1;
  fd0 = -1;
  if((fd0 = fdalloc(rf)) < 0 || (fd1 = fdalloc(wf)) < 0){
    if(fd0 >= 0)
      proc->ofile[fd0] = 0;
    fileclose(rf);
    fileclose(wf);
    return -1;
  }
  fd[0] = fd0;
  fd[1] = fd1;
  return 0;
}
