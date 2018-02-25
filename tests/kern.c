#include "xu_kern.h"

int main(int argc, char *argv[])
{
	xu_kern_init(argc, argv);
	xu_actor_new("echo", "");
	xu_kern_start();
	return 0;
}

