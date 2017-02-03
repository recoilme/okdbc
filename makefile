debug: okdb.c sophia.c
	cc -std=c99 -o okdb okdb.c sophia.c -lpthread -levent -I/usr/local/include -L/usr/local/lib
build: okdb.c sophia.c
	cc -O2 -DNDEBUG -std=c99 -pedantic -Wall -Wextra -o okdb okdb.c sophia.c -lpthread -levent -I/usr/local/include -L/usr/local/lib
