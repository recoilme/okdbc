# Install libevent

Usefull link: https://github.com/libevent/libevent

Example for mac os, without openSSL

```
wget https://github.com/libevent/libevent/releases/download/release-2.1.8-stable/libevent-2.1.8-stable.tar.gz
tar -xvzf libevent-2.1.8-stable.tar.gz
cd libevent-2.1.8-stable
./configure --disable-openssl
make
sudo make install
```

```
cc test.c commands.c -o test && ./test
gcc -o pudge pudge.c shared.c sophia.c server.c workqueue.c commands.c -levent -lpthread -L/usr/local/Cellar/libevent/HEAD-3821cca/lib -I/usr/local/Cellar/libevent/HEAD-3821cca/include && ./pudge
gcc -o pudge pudge.c sophia.c server.c workqueue.c commands.c -levent -lpthread -L/home/vkulibaba/libevent-2.0.22-stable/.libs -I/home/vkulibaba/libevent-2.0.22-stable/include/

printf "set key 0 0 3\r\nval\r\n" | nc 127.0.0.1 5555

https://github.com/recoilme/cliserver/blob/master/cliserver.c
https://github.com/recoilme/hashtable/blob/master/src/server.c
https://github.com/pmwkaa/sophia
https://github.com/recoilme/GoHttp/blob/master/GoHttp.c
https://github.com/recoilme/sandbird/blob/master/src/sandbird.c
http://www.wangafu.net/~nickm/libevent-2.0/doxygen/html/buffer_8h.html
https://github.com/memcached/memcached/blob/master/doc/protocol.txt
```