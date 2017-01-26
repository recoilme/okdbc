# okdb
A fast, light-weight key/value store with http interface

## dependencies
okdb based on two brilliant projects:

Your powerful network backend in C, with HTTP and Websocket support http://facil.io

Modern transactional key-value/row storage library. http://sophia.systems

## build & run
```
git submodule init
git submodule update
make build
./tmp/okdb
```

## run
Open http://localhost:8888/ and you must see "OK" message.

## status
In development

## test
Test with keep alive connection: ab -n 1000 -c 200 -k http://127.0.0.1:8888/

Requests per second:    65750.11 - MacBook Pro (Retina, 13-inch, Early 2015)

## use
### For set/put new key/value you must send PUT request:
```
curl -X PUT -d "world" http://127.0.0.1:8888/hello
```
This will add key "hello" with value "world"

### For get value by key you must send GET request:
```
curl http://127.0.0.1:8888/hello
```
You will see "world"

## params
Params defined in okdb.c file:
```
#define THREAD_COUNT 1 // depending on your use-case, you might want more threads.

#define PROCESS_COUNT 4 // leave one core for the kernel and the ab test tool

#define PORT "8888" // default port

#define LOG_LEVEL 1 // 1 print debug info

#define KEY_MAX_SIZE 1024 // max key size

#define VALUE_MAX_SIZE 4096 // max value size
```

