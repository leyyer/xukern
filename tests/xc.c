#include "xu_core.h"

int main(int argc, char *argv[])
{
	xuctx_t ctx;

	xu_core_init(argc, argv);

	xu_setenv("bootstrap", "loader.lua");
	//xu_env_dump("test.json");

	ctx = xu_ctx_new();

	xu_ctx_load(ctx, argv[1]);

	xu_ctx_run(ctx);

	xu_core_exit();
	return 0;
}

