#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <assert.h>
#include <string.h>

#include "server.h"
#include "sophia.h"
#include "shared.h"

void *env;
void *db;

int init() {
	/* open or create environment and database */
    env = sp_env();
	sp_setstring(env, "sophia.path", "_test", 0);
	sp_setstring(env, "db", "test", 0);
	sp_setint(env, "db.test.mmap", 1);
	db = sp_getobject(env, "db.test");
    return sp_open(env);
}

int db_set(char *key,char *val) {
    /* set */
	void *o = sp_document(db);
	sp_setstring(o, "key", &key[0], strlen(key));
	sp_setstring(o, "value", &val[0], strlen(val));
	return sp_set(db, o);
}

char *db_get(char *key) {
	/* get */
	void *o = sp_document(db);
	sp_setstring(o, "key", &key[0], strlen(key));
	o = sp_get(db, o);
	if (o) {
		/* ensure key and value are correct */
		int size;
		char *ptr = sp_getstring(o, "value", &size);
		char *ptr2 = strndup_p(ptr, size);
		//assert(size == strlen(key));
		//assert(*(char*)ptr == key);

		//ptr = sp_getstring(o, "value", &size);
		//assert(size == strlen(val));
		//assert(*(char*)ptr == key);

		sp_destroy(o);
        return ptr2;//*(char*)ptr;
	}
    return "";
}

int db_del(uint32_t key) {
    /* delete */
	void *o = sp_document(db);
	sp_setstring(o, "key", &key, sizeof(key));
	return sp_delete(db, o);
}

int main(int argc, char *argv[])
{
	(void)argc;
	(void)argv;

	if (init() == -1)
		goto error;

    if (db_set("hello","world") == -1)
		goto error;

    char *val = db_get("hello");
    printf("val:%s\n",val);

	//if (db_del(2) == -1)
	//	goto error;
    runServer();
	/* finish work */
	sp_destroy(env);
	return 0;

error:;
	int size;
	char *error = sp_getstring(env, "sophia.error", &size);
	printf("error: %s\n", error);
	free(error);
	sp_destroy(env);
	return 1;
}