#include "types.h"
#include "stat.h"
#include "user.h"
#include "fs.h"

void
filemove(char *srcpath, char *destpath)
{
	if(move(srcpath, destpath) < 0)
	{
		printf(2, "mv: %s failed to move\n", srcpath);
		return;
	}
}

int
main(int argc, char *argv[])
{
	if(argc < 3)
	{
		printf(2,"Usage: mv srcpath destpath...\n");
	    exit();
	}
	filemove(argv[1], argv[2]);
	exit();
}
