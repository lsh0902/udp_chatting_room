p2p chatting room

======Compile======
sudo gcc -o server server.c -lpthread -lm
sudo gcc -o peer peer.c -lpthread
====================
To execute
./server port
./peer server-ip server-port my-port

ex) ./server 8000
    ./peer 192.168.46.15 8000 8000

====================
server command
-i

peer command
-r
-i
-j room_num
-c room_name
-l
-m message
