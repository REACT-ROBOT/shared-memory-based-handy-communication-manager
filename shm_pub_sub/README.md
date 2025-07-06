# shm_pub/sub(shared memory publish/subscribe)

## About
This library is the publisher/subscriber model communication library with shared memory.

## Motivation


## How to build
This package supports cmake as build tool.
Please clone this package under ${YOUR_WS}/src/ and build it.

## How to Run Sample
Please run below commands in two terminals.
```
(Terminal 1)
${YOUR_WS}/build/src/shared-memory-based-handy-communication-manager/shm_pub_sub/sample/shm_pub_sub_sample -w

(Terminal 2)
${YOUR_WS}/build/src/shared-memory-based-handy-communication-manager/shm_pub_sub/sample/shm_pub_sub_sample -r
```
Terminal 1 writes user-defined class to shared memory.
And Terminal 2 reads it from shared memory.

## How to Use with Python
This package can be called with Python.
Please run below sample commands.
```
(Terminal 1)
export PYTHONPATH=$PYTHONPATH:${YOUR_WS}/build/python

python3
> import shm_pub_sub
> pub = shm_pub_sub.Publisher("test", int(), 3)
> pub.publish(8)

(Terminal 2)
export PYTHONPATH=$PYTHONPATH:${YOUR_WS}/build/python

python3
> import shm_pub_sub
> sub = shm_pub_sub.Subscriber("test", int())
> result = sub.subscribe()
> print(result)
```

## How to Build Document
```
doxygen ./DoxyConfig
```
The document is built in ./doc/.
Please open ./docs/index.html with web browser.
