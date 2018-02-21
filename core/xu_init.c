#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include "cJSON.h"
#include "uv.h"
#include "xu_malloc.h"
#include "xu_util.h"
#include "xu_impl.h"

static void __parsing(int argc, char *argv[])
{
	int i, c;
	char *agv[argc];
	/* parse arguments, copy argvs locals to parse.*/
	for (i = 0; i < argc; ++i) {
		agv[i] = xu_strdup(argv[i]);
	}

	while ((c = getopt(argc, agv, "c:?")) != -1) {
		switch (c) {
			case 'c':
				xu_env_load(optarg);
				break;
		}
	}

	for (i = 0; i < argc; ++i) {
		xu_free(agv[i]);
	}
}

static void *__malloc(size_t sz)
{
	return xu_malloc(sz);
}

int xu_core_init(int argc, char *argv[])
{
	static cJSON_Hooks hook = {
		__malloc,
		xu_free
	};

	signal(SIGPIPE, SIG_IGN);

	cJSON_InitHooks(&hook);
	uv_replace_allocator(__malloc, xu_realloc, xu_calloc, xu_free);

	xu_envinit();

	__parsing(argc, argv);

	return 0;
}

void xu_core_exit(void)
{
	xu_envexit();
}

