#include "types.h"
#include "stat.h"
#include "user.h"
#include "fs.h"

void
filecreate(char *path)
{
	if(touch(path) < 0)
	{
		printf(2, "touch: %s failed to create\n", path);
		return;
	}
}

int
main(int argc, char *argv[])
{
	if(argc < 2)
	{
		printf(2,"Usage: touch filepath...\n");
	    exit();
	}
	filecreate(argv[1]);
	exit();
}