
# DPDK backend

This is a DPDK application that performs high-speed TCP RTT tracking. It publishes results on ZMQ sockets.

The output format on the sockets is the following:

```
LAT-xxxxxxxx-yyyyyyyy-0000000011-0000000044-
```

where:

- LAT is just a label to filter between multiple messages in the future
- xxxxxxxxx is the source IPv4 address in hex
- yyyyyyyyy is the destination IPv4 address in hex
- 00000000011 is the latency (RTT) measured between the source host and our measurement tap
- 00000000044 is the latency (RTT) measured between the measurement tap and the destination host

## Running it

After you have complied this app, you will have a binary in your build folder. 

Before running the app, You will need to bind interfaces to DPDK (at least one). Use the dpdk-devbind.py utility provided with DPDK to do this.
You should see something like this:

```sh
rcziva@m5:~/dpdk-latency/dpdk$ ~/dpdk/tools/dpdk-devbind.py --status

Network devices using DPDK-compatible driver
============================================
0000:03:00.0 '82599ES 10-Gigabit SFI/SFP+ Network Connection' drv=igb_uio unused=uio_pci_generic
0000:03:00.1 '82599ES 10-Gigabit SFI/SFP+ Network Connection' drv=igb_uio unused=uio_pci_generic

```

You can now start the application with a command similar to this:

```sh
$ sudo ./build/dpdk-latency -c ff -n 4 -- -p ff -T 60 --config "(0,0,1),(0,1,2),(0,2,3),(0,3,4),(0,4,5),(0,5,6),(1,0,1),(1,1,2),(1,2,3),(1
,3,4),(1,4,5),(1,5,6)" --forwarding
```

The --config option specifies the (port, queue, lcore) config.

If you are using the --forwarding option, the app behaves like a small bridge - it forwards all packets from port 0 to port 1 and from port 1 to port 0. If you have more than ttwo ports, this will not work out of the box. Actually, this is options is designed for debugging and performance validation purposes.

The app prints out port statistics periodically. You can specify the time between updates with -T. In production deployment, I recommend -T 60 at least (or larger).

## Installing dependencies and DPDK

Install DPDK and compile it for your target platform. Get DPDK from here: http://dpdk.org.

Modify the Makefile to point to your DPDK directory.

Install ZMQ

```sh
$ sudo apt-get install libzmq3-dev
```

To compile this application, you can use the Makefile provided:

```sh
$ make
```

## Help

If you need help, don't hesitate to contact me on r.cziva.1@research.gla.ac.uk.
