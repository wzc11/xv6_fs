#include "types.h"
#include "stat.h"
#include "user.h"

int
main(int argc, char *argv[])
{
  int i;

  if(argc < 2){
    printf(2, "Usage: rm files or rm -r dir...\n");
    exit();
  }

  // modified: 12.30 15:00
  if (strcmp(argv[1], "-r") == 0 || strcmp(argv[1], "-R") == 0) { // with -r
    if (argc != 3) { // wrong usage
      printf(2, "rm: too much arguments...\n");
      exit();
    } else { // remove folder
      if (remove(argv[2]) < 0) {
        printf(2, "rm: %s failed to delete folder\n", argv[2]);
        exit();
      }
    }
  } else { // without -r
    for(i = 1; i < argc; i++){
      if(unlink(argv[i]) < 0){
        printf(2, "rm: %s failed to delete\n", argv[i]);
        exit();
      }
    }
  }

  exit();
}
