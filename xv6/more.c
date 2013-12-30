// Simple grep.  Only supports ^ . * $ operators.

#include "types.h"
#include "stat.h"
#include "user.h"

char buf[1024];
char key[2];

void
more(int fd)
{
  int n, m, count;
  char *p, *q;
  
  m = 0;
  count = 0;
  while((n = read(fd, buf+m, sizeof(buf)-m)) > 0){
    m += n;
    p = buf;
    while((q = strchr(p, '\n')) != 0){
      *q = '\n';
      write(1, p, q+1 - p);
      *q = 0;
      p = q+1;
      count++;
      if(count == 10)
      {
        printf(1,"Press 'C' to continue. Press 'Q' to quit\n");
        gets(key, sizeof(char)*2);
        if(strcmp(key,"c") == 0)
        {
          count = 0;
        }          
        else if(strcmp(key,"q") == 0)  //Esc
          return;
      }
    }
    if(p == buf)
      m = 0;
    if(m > 0){
      m -= p - buf;
      memmove(buf, p, m);
    }
  }
}

int
main(int argc, char *argv[])
{
  int fd;
    
  if(argc < 2){
    printf(2, "usage: more filename ...\n");
    exit();
  }
  if((fd = open(argv[1], 0)) < 0){
    printf(1, "more: cannot open %s\n", argv[1]);
    exit();
  }
  more(fd);
  close(fd);
  exit();
}
