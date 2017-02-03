# okdb
A fast, light-weight key/value store with http & memcache interface.

okdb implements a high-level cross-platform sockets interface to sophia db.

okdb is fast, effective and simple.

## dependencies
### Sophia - modern transactional key-value/row storage library: http://sophia.systems
Sophia included as amalgamation build (version 2.2).

### Libevent - an event notification library: http://libevent.org/
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
make
./okdb 11213 >> error.log &
```

## run
Open http://localhost:11213/ and you must see "OK" message.

## status
In development

## test
Test with keep alive connection: ab -n 1000 -c 200 -k http://127.0.0.1:11213/

Requests per second:    65750.11 - MacBook Pro (Retina, 13-inch, Early 2015)

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
>VALUE hello 0 5
world
END
quit
```
## http interface
### For set new value you must send PUT request:
```
//in development
curl -X PUT -d "world" http://127.0.0.1:11213/hello
```
This will add key "hello" with value "world"

### For get value you must send GET request:
```
curl http://127.0.0.1:11213/hello
```
You will see "world"