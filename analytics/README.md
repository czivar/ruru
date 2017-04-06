
## Dependencies 

### Install IP2Location.h

After running setup.sh from the root directory, you should have the source of IP2Location.h in deps/IP2Location. Follow these steps to install:

    cd deps/IP2Location-C-Library
    sudo apt-get install automake libtool
    autoreconf -i -v --force
    ./configure
    sudo make install
    sudo ldconfig

### Install libsqlite

Install sqlite libraries with the following command:

    sudo apt-get install sqlite3 libsqlite3-dev

### Install json-c

    cd deps/json-c
    ./setup.sh
    ./configure
    sudo make install
    sudo ldconfig

### Install libcurl

    sudo apt-get install libcurl4-gnutls-dev

## Setting up InfluxDB

You should install Docker if you don't have it already on your system. Use this guide: https://docs.docker.com/engine/installation/linux/ubuntu/

Start a command similar to this (change path to an empty directory where data will be stored)

    sudo docker run -d -p 8083:8083 -p 8086:8086 --name influxcontainer -v /home/rcziva/ruru/influxdb:/var/lib/influxdb influxdb

Set the retention policy for Influx

    sudo docker exec -ti influxcontainer influx 
    > create database rtt
    > alter retention policy autogen on rtt duration 520w 

## Setting up sqlite database for ASN lookups

Download an ASN database in a .csv format from this site (it is free): http://lite.ip2location.com/database/ip-asn (you will need to register). After downloading, you need to import this csv file to the asn.db database. I have prepared a script for you to do this - you can find it in importcsv.sql

    cd data
    sqlite asn.db
    run the contents of importcsv.sql here

## Getting the IP2Location database

You can get a free IP2Location database from here: http://lite.ip2location.com/database/ip-country-region-city-latitude-longitude (I am using DB5). It is important that you need the .bin format (not .csv). Once downloaded, move it to the data folder and name it ip2location-db5.bin.

    scp /home/clouduser/IP2LOCATION-LITE-DB5.BIN data/ip2location-db5.bin

## Building the analytics module and running

Once you have all the dependencies installed, you can build the analytics module with make.

    make

To run it, you need to specify a few zmq sockets: sockets it read data from and sockets it will set up to publish data on.

    ./analytics --bind tcp://0.0.0.0:5502 tcp://0.0.0.0:5503 tcp://0.0.0.0:5504 tcp://0.0.0.0:5505 tcp://0.0.0.0:5506 tcp://0.0.0.0:5507 --publish tcp://127.0.0.1:6080 --influx http://127.0.0.1:8086/write?db=rtt

If you see this output, it is running:

    Publishing on: tcp://0.0.0.0:6000
    Subscribing on: tcp://127.0.0.1:5502
    Subscribing on: tcp://127.0.0.1:5503
    Trying to start listener thread on tcp://127.0.0.1:5502
     Thread 0 created successfully
    IP2Location API version: 8.0.3 (80003)
    Trying to start listener thread on tcp://127.0.0.1:5503
     Thread 1 created successfully
    IP2Location API version: 8.0.3 (80003) 


## (Optional) Run grafana 

With Grafana, you can browse the Influx DB from a web browser. You can start it in a container with a similar command to have it listening on port 4000:

    docker run -d -p 4000:3000 -v /home/rcziva/ruru/grafana:/var/lib/grafana -e "GF_SECURITY_ADMIN_PASSWORD=banana" grafana/grafana

