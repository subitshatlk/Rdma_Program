# Rdma_Program
Demo - Addition of two numbers using RDMA

To compile and run 

For server : 

~ cc -o server server.c -lrdmacm -libverbs

~ ./server

For client: 

~ cc -o client client.c -lrdmacm -libverbs
~ ./client <server-ip-addr> [val1] [val2]   
~ ./client <server-ip-addr> 2 2    
