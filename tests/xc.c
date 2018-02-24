#include "xu_core.h"
#include "xu_util.h"

int main(int argc, char *argv[])
{
	int r;
	xuctx_t ctx;

	xu_core_init(argc, argv);

	xu_setenv("bootstrap", "loader.lua");
	//xu_env_dump("test.json");

	ctx = xu_ctx_new();

	xu_ctx_load(ctx, argv[1]);
	r = xu_ctx_run(ctx);
	xu_println("loop end: %d", r);
	xu_core_exit();
	return 0;
}

