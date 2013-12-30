#include "types.h"
#include "stat.h"
#include "user.h"
#include "fs.h"

void
filecopy(char *srcpath, char *destpath)
{
	if(copy(srcpath, destpath) < 0)
	{
		printf(2, "cp: %s failed to copy\n", srcpath);
		return;
	}
}

int
main(int argc, char *argv[])
{
	if(argc < 3)
	{
		printf(2,"Usage: cp srcpath destpath...\n");
	    exit();
	}
	filecopy(argv[1], argv[2]);
	exit();
}