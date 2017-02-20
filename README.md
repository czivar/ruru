# Ruru: real-time TCP latency monitoring
'Ruru' is a TCP latency monitoring application that helps understanding wide area TCP traffic in real time. 
It utilises Intel DPDK for high speed packet processing (up to 40Gbit/s) and a Node.JS web frontend to present results.

![Ruru live](/animation.gif)



## High-level architecture

The system componses of three parts:
- DPDK-latency backend (written in C / multi-threaded): This software measures the elapsed time between SYN, SYN-ACK and the first ACK TCP packets for all TCP streams. It sends the measurement information (source IP, destination IP, latency (in microsecond)) on a ZMQ sockets.
- Analytics (written in C / multi-threaded): This component retrieves AS / geotag information for all IPs in the measurement data received from the DPDK backend and generates basic statistics. It pushes information in JSON format on ZMQ sockets.
- Frontend: It is a Node.js built with React and Deck.Gl. It uses socket.io to communicate with the browser.

Communication between components uses sockets (zmq and websockets). The high-level architecture is shown below.

<p align="center">
<img alt="Architecture" width="650px" src="https://github.com/REANNZ/cooee/raw/master/architecture.png" />
</p>

# Installation

Installation consists of the following steps:

1. Install dpdk with by running ./setup.sh
2. Compile dpdk-latency with make
3. Set up analytics (more details in the README file of the analytics)
4. Set up the frontend (more details in the README file of the frontend)

## Frequently asked questions

### What does it measure?

It measures TCP handshakes for each individual flow: the time it takes to set up a TCP connection. It looks at TCP flags (SYN, SYN-ACK, first ACK) of the TCP packets (and nothing else).

### What processing performance does Ruru provide?

The core of Ruru (that measures the latency for each flow) is based on DPDK, therefore it can cope with up to 40Gbit/s traffic. Geo-localising each source and destination IP address takes a lot of CPU cycles, therefore it mostly depends on how powerful your host machine is. We have deployed Ruru tapping a 10Gbit/s link.

### Which NICs do you support?

All DPDK supported NICs can be used for Ruru. All supported NICs can be found here: http://dpdk.org/doc/nics. We used Intel X520 NICs at REANNZ.

In case of any other questions, please contact Richard Cziva (r.cziva.1@research.gla.ac.uk)
