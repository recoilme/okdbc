# okdb
A fast, light-weight key/value store with http & memcache(TCP/UDP) interface.

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
simplified, with custom port (default 11213):
./okdb 9876 >> error.log &
or with full list custom params:
./okdb -p 9876 -D -sophia.path ~/tmp2 -backup.path ~/tmp22
(start on port 9876, with debug log, with sophiadb in folder ~/tmp2 & backup folder ~/tmp22)
```

## run
Open http://localhost:11213/ and you must see "OK" message.

## status
In development

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

## short description text memcache protocol

#### set
```
set [key] 0 0 [sizeof(value)] [noreply]
value
```

where

- key - must be a string without ?,\r,\n and so on simbols
- 0 0 - reserved bytes
- sizeof(value) - size of value in bytes (without zero terminate byte)
- noreply - dont send reply
- response: STORED\r\n or NOT_STORED\r\n

#### get
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


#### quit

quit\r\n - terminate session and close connection


#### cursor
```
get ?type=cursor&prefix=key&limit=1
```
where
- type=cursor - start iterator
- prefix - key prefix - default empty("")
- limit - limit result - default 100

It will return keys/values >= prefix in descending order (for same sizes keys)

For example you have keys:
```
r:99:105
r:100:123
r:101:100
r:102:9
r:1000:1
```
For query 'get ?type=cursor&prefix=r:10', output keys will be: 
```
r:102:9
r:101:100
r:100:123
r:1000:1
```

#### sophia
You may access sophia api via memcache protocol.

Available methods: sp_setstring, sp_getstring, sp_setint, sp_getint

Sintax

- type=sophia
- command=getstring[setstring,setint,getint]
- key=some sophia key, list of keys: http://sophia.systems/v2.2/conf/sophia.html
- value (for 'set' commands - int or string)

Set command return 0 on success and -1 on error (you may find error in log or with key sophia.error)

Get commands return int64_t or string

Examples:
```
// get version
get get ?type=sophia&command=getstring&key=sophia.version
VALUE sophia.version 0 4
2.2
END
// get status
get ?type=sophia&command=getstring&key=sophia.status
VALUE sophia.status 0 7
online
END
// get last error
get ?type=sophia&command=getstring&key=sophia.error
VALUE sophia.error 0 60
sophia/runtime/sr_conf.c:339 bad configuration path: status
END
// get index count
get ?type=sophia&command=getint&key=db.db.index.count
VALUE db.db.index.count 0 8
34587306
END
// run backup
get ?type=sophia&command=setint&key=backup.run&value=0
VALUE backup.run 0 1
0
END
// get last backup
get ?type=sophia&command=getint&key=backup.last
VALUE backup.last 0 1
1
END

```

## http interface (in development)

#### For get value you must send GET request:
```
curl http://127.0.0.1:11213/hello
```
You will see value and status 200 or NOT_FOUND and status 404

Example response:
```
GET /key

HTTP/1.1 200 OK
Connection: Keep-Alive
Content-Type: text/html; charset=UTF-8
Content-Length: 5
Keep-Alive: timeout=20, max=200
Server: okdb/0.0.1

value
```
Http send keep-Alive header

Main page response is: 

OK\r\n

## UDP interface

okdb support memcache protocol via udp

## Replication

You may set one replication database

Master will send set commands to replica over udp protocol

Example master config:
```
./okdb -p 11213 -subhost 127.0.0.1 -subport 11214 -D -sophia.path 1 -backup.path 1
where:
subhost 127.0.0.1 - replication ip
subport 11214 - replication port
```
Example replication config:
```
./okdb -p 11214 -D -sophia.path 2 -backup.path 2
```

## Test results

### MacBook Pro (Retina, 13-inch, Early 2015, 2-cores)
#### ab
```
ab -n 1000 -c 200 -k http://127.0.0.1:11213/key
This is ApacheBench, Version 2.3 <$Revision: 1748469 $>
Copyright 1996 Adam Twiss, Zeus Technology Ltd, http://www.zeustech.net/
Licensed to The Apache Software Foundation, http://www.apache.org/

Benchmarking 127.0.0.1 (be patient)
Completed 100 requests
Completed 200 requests
Completed 300 requests
Completed 400 requests
Completed 500 requests
Completed 600 requests
Completed 700 requests
Completed 800 requests
Completed 900 requests
Completed 1000 requests
Finished 1000 requests


Server Software:        okdb/0.0.1
Server Hostname:        127.0.0.1
Server Port:            11213

Document Path:          /key
Document Length:        5 bytes

Concurrency Level:      200
Time taken for tests:   0.029 seconds
Complete requests:      1000
Failed requests:        0
Keep-Alive requests:    1000
Total transferred:      160000 bytes
HTML transferred:       5000 bytes
Requests per second:    34943.04 [#/sec] (mean)
Time per request:       5.724 [ms] (mean)
Time per request:       0.029 [ms] (mean, across all concurrent requests)
Transfer rate:          5459.85 [Kbytes/sec] received

Connection Times (ms)
              min  mean[+/-sd] median   max
Connect:        0    1   3.1      0      11
Processing:     2    3   0.7      3       4
Waiting:        2    3   0.7      3       4
Total:          2    5   3.2      3      14

Percentage of the requests served within a certain time (ms)
  50%      3
  66%      4
  75%      4
  80%      7
  90%     11
  95%     12
  98%     13
  99%     13
 100%     14 (longest request)
```
#### wrk
```
wrk -c 200 http://127.0.0.1:11213/key
Running 10s test @ http://127.0.0.1:11213/key
  2 threads and 200 connections
  Thread Stats   Avg      Stdev     Max   +/- Stdev
    Latency     3.09ms  314.78us  10.82ms   84.44%
    Req/Sec    32.44k     0.91k   33.46k    91.58%
  652064 requests in 10.11s, 99.50MB read
Requests/sec:  64525.69
Transfer/sec:      9.85MB
```
#### https://github.com/antirez/mc-benchmark

```
./mc-benchmark -h 127.0.0.1 -p 11213 -c 100 -n 10000 -k 1
====== SET ======
  10005 requests completed in 0.20 seconds
  100 parallel clients
  3 bytes payload
  keep alive: 1

11.22% <= 1 milliseconds
89.69% <= 2 milliseconds
99.22% <= 3 milliseconds
100.00% <= 4 milliseconds
48804.88 requests per second

====== GET ======
  10002 requests completed in 0.18 seconds
  100 parallel clients
  3 bytes payload
  keep alive: 1

40.11% <= 1 milliseconds
92.60% <= 2 milliseconds
98.95% <= 3 milliseconds
99.92% <= 4 milliseconds
100.00% <= 5 milliseconds
56191.01 requests per second
```

#### Similar benchmark for memcached-1.4.34 (memcache store data in memory, so it's more fast then okdb)

```
mc-benchmark -h 127.0.0.1 -p 11211 -c 100 -n 10000 -k 1
====== SET ======
  10011 requests completed in 0.14 seconds
  100 parallel clients
  3 bytes payload
  keep alive: 1

0.75% <= 0 milliseconds
64.30% <= 1 milliseconds
96.04% <= 2 milliseconds
99.19% <= 3 milliseconds
99.93% <= 4 milliseconds
100.00% <= 5 milliseconds
69520.84 requests per second

====== GET ======
  10010 requests completed in 0.19 seconds
  100 parallel clients
  3 bytes payload
  keep alive: 1

0.37% <= 0 milliseconds
37.61% <= 1 milliseconds
90.40% <= 2 milliseconds
97.15% <= 3 milliseconds
99.38% <= 4 milliseconds
99.66% <= 5 milliseconds
99.74% <= 6 milliseconds
99.78% <= 7 milliseconds
99.81% <= 8 milliseconds
99.85% <= 9 milliseconds
99.86% <= 10 milliseconds
99.89% <= 11 milliseconds
99.91% <= 12 milliseconds
99.93% <= 13 milliseconds
99.97% <= 14 milliseconds
100.00% <= 15 milliseconds
53244.68 requests per second
```