all: kill debug run
debug:
	cc -o okdb okdb.c sophia.c -lpthread -levent -I/usr/local/include -L/usr/local/lib
build:
	cc -O2 -DNDEBUG -pedantic -Wall -Wextra -o okdb okdb.c sophia.c -lpthread -levent -I/usr/local/include -L/usr/local/lib
run: 
	./okdb
kill:
	-pkill okdb && -rm ./okdb
