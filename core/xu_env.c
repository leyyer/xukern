#include <string.h>
#include <stdlib.h>
#include "xu_impl.h"
#include "cJSON.h"

#define ENVIRON_SECTION  "environ"

struct environ {
	struct environ *next;
	char *env;
	char *val;
};

struct xu_env {
	struct spinlock lock;
	struct environ *head, **tail;
};

static struct xu_env __E[1];

void xu_envinit(void)
{
	SPIN_INIT(__E);
	__E->head = NULL;
	__E->tail = &__E->head;
}

void xu_envexit(void)
{
	struct environ *er, *next;

	SPIN_LOCK(__E);
	er = __E->head;
	while (er) {
		next = er->next;
		xu_free(er);
		er = next;
	}
	SPIN_UNLOCK(__E);
}

void xu_env_map(int (*map)(void *ud, const char *key, const char *value), void *ud)
{
	struct environ *er;
	int r;

	SPIN_LOCK(__E);
	er = __E->head;
	while (er) {
		r =	map(ud, er->env, er->val);
		if (r != 0)
			break;
		er = er->next;
	}
	SPIN_UNLOCK(__E);
}

const char *xu_getenv(const char *env, char *buf, size_t size)
{
	struct environ *eir;
	const char *p = NULL;

	SPIN_LOCK(__E);
	eir = __E->head;
	while (eir) {
		if (strcmp(eir->env, env) == 0) {
			if (buf && size > 0) {
				xu_strlcpy(buf, eir->val, size);
				p = buf;
			} else {
				p = eir->val;
			}
			break;
		}
		eir = eir->next;
	}
	SPIN_UNLOCK(__E);
	return p;
}

void xu_setenv(const char *env, const char *value)
{
	struct environ *er, *old = NULL, **itor;
	size_t es, vs;

	es = strlen(env) + 1;
	vs = strlen(value) + 1;

	er = xu_malloc(sizeof *er + es + vs);
	er->env  = (char *) &er[1];
	er->val  = er->env + es;
	er->next = NULL;
	memcpy(er->env, env, es);
	memcpy(er->val, value, vs);

	SPIN_LOCK(__E);
	/* find the old one. */
	itor = &__E->head;
	while (*itor) {
		if (strcmp((*itor)->env, env) == 0) {
			old = *itor;
			*itor = (*itor)->next;
			if (__E->head == NULL) {
				__E->tail = &__E->head;
			}
			break;
		}
		itor = &(*itor)->next;
	}
	/* remove the old one, if it have it. */
	if (old) {
		xu_free(old);
	}
	/* add the environ to tail */
	*__E->tail = er;
	__E->tail  = &er->next;
	SPIN_UNLOCK(__E);
}

void xu_env_load(const char *file)
{
	FILE *sfp;
	char *buf;
	int len;
	cJSON *root, *env;

	sfp = fopen(file, "r");
	if (sfp == NULL)
		return;

	fseek(sfp, 0L, SEEK_END);
	len = ftell(sfp);
	fseek(sfp, 0L, SEEK_SET);

	buf = xu_calloc(1, len + 1);
	fread(buf, 1, len, sfp);
	fclose(sfp);

	root = cJSON_Parse(buf);

	xu_free(buf);

	if (!root)
		return;

	/* parse `environ's */
	env = cJSON_GetObjectItem(root, ENVIRON_SECTION);
	if (env) {
		env = env->child;
		while (env) {
			if ((buf = cJSON_GetStringValue(env)) != NULL) {
				xu_setenv(env->string, env->valuestring);
			}
			env = env->next;
		}
	}
	cJSON_Delete(root);
}

static int __dump(void *ud, const char *env, const char *val)
{
	cJSON *j = ud;
	cJSON_AddStringToObject(j, env, val);
	return 0;
}

void xu_env_dump(const char *file)
{
	cJSON *root, *env;
	char *out;

	root = cJSON_CreateObject();
	env = cJSON_CreateObject();
	xu_env_map(__dump, env);

	cJSON_AddItemToObject(root, ENVIRON_SECTION, env);

	out = cJSON_Print(root);

	cJSON_Delete(root);

	if (out) {
		FILE *sfp;

		sfp = fopen(file, "w");
		if (sfp) {
			fprintf(sfp, "%s\n", out);
			fclose(sfp);
		}
		xu_free(out);
	}
}

