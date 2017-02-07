# okdb
A fast, light-weight key/value store with http & memcache interface.

okdb implements a high-level cross-platform sockets interface to sophia db.

okdb is fast, effective and simple.


## dependencies
### Sophia - modern transactional key-value/row storage library: 
http://sophia.systems

Sophia included as amalgamation build (version 2.2).

### Libevent - an event notification library: 
http://libevent.org/

Example how to build libevent 2.1.8 for mac os, without openSSL

```
wget https://github.com/libevent/libevent/releases/download/release-2.1.8-stable/libevent-2.1.8-stable.tar.gz
tar -xvzf libevent-2.1.8-stable.tar.gz
cd libevent-2.1.8-stable
./configure --disable-openssl
make
sudo make install
``` 

## build & run
```
git clone https://github.com/recoilme/okdb.git
cd okdb
make build
./okdb 
with custom port (default 11213):
./okdb 9876 >> error.log &
```

## run
Open http://localhost:11213/ and you must see "OK" message.

## status
In development

## test
Test with keep alive connection: ab -n 1000 -c 200 -k http://127.0.0.1:11213/

Requests per second:    29836.50 - MacBook Pro (Retina, 13-inch, Early 2015)

## memcache interface

okdb partialy support text based memcache protocol.

Supported commands:
    get
    set
    quit

Example:
```
telnet localhost 11213
set key 0 0 5
value
>STORED
get key
>VALUE value 0 5
>world
>END
get ?type=cursor&prefix=key&limit=1
>VALUE key 0 5
>value
>END
quit
```
See demo

![demo](https://github.com/recoilme/okdb/blob/master/ok.gif?raw=true)

## description memcache protocol

```
set [key] 0 0 [sizeof(value)]
value
```

where

- key - must be a string without ?,\r,\n and so on simbols
- 0 0 - reserved bytes
- sizeof(value) - size of value in bytes (without zero terminate byte)
- response: STORED\r\n or NOT_STORED\r\n

```
get [key]
```
- key - stored key
- response if key found (if not found  - return END\r\n):
```
VALUE value 0 5
world
END
```
where
- value - name of key
- 5 - sizeof(value) - in bytes

multiple get

get ?type=cursor&prefix=key&limit=1

where
- type=cursor - start iterator
- prefix - key prefix (will return) - default empty
- limit - limit result - default 100

It will return keys/values in descending order (for same sizes keys)

For example you have keys:
```
r:99:105
r:100:123
r:101:100
r:102:9
r:1000:1
```
For query 'get ?type=cursor&prefix=r:10', output key will be: 
```
r:102:9
r:101:100
r:100:123
r:1000:1
```

quit - terminate session



## http interface (in development)

### For get value you must send GET request:
```
curl http://127.0.0.1:11213/hello
```
You will see "world" or and stus 200 or NOT_FOUND and status 404 if not found

Main page response is: OK\r\n