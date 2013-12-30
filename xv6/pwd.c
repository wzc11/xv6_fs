#include "types.h"
#include "stat.h"
#include "user.h"
#include "param.h"
#include "inode.h"

int main()
{
    int path_max = 100;
    char current_work_dir[path_max];
    memset(current_work_dir, 0, path_max);
    if (getcwd(current_work_dir,path_max) != 0){
    	printf(2, "Couldn`t get current working directory!\n");
    	return 1;
    }else{
    	printf(2, "%s\n",current_work_dir);
    }
    exit();
}