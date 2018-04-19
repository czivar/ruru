
## Dependencies 

## Data dependency - setting up sqlite database for ASN lookups

Download an ASN database in a .csv format from this site (it is free): http://lite.ip2location.com/database/ip-asn (you will need to register). After downloading, you need to import this csv file to the asn.db database. I have prepared a script for you to do this - you can find it in importcsv_asn.sql

    cd data
    sqlite asn.db
    run the contents of importcsv_asn.sql here

## Data dependency - setting up sqlite database for proxied IPs

Download the Proxy database in a .csv format from this site (it is free): http://lite.ip2location.com/database/px4-ip-proxytype-country-region-city-isp (you will need to register). After downloading, you need to import this csv file to the proxy.db database. I have prepared a script for you to do this - you can find it in importcsv_proxy.sql

    cd data
    sqlite proxy.db
    run the contents of importcsv_proxy.sql here


## Data dependency - getting the IP2Location database

You can get a free IP2Location database from here: http://lite.ip2location.com/database/ip-country-region-city-latitude-longitude (I am using DB5). It is important that you need the .bin format (not .csv). Once downloaded, move it to the data folder and name it ip2location-db5.bin.

    scp /home/clouduser/IP2LOCATION-LITE-DB5.BIN data/ip2location-db5.bin

## Init submodules

    git submodule update --init --recursive


## Setting up InfluxDB

Start a command similar to this (change path to an empty directory where data will be stored)

    sudo docker run -d -p 127.0.0.1:8086 --name influxcontainer -v /home/rcziva/ruru/influxdb:/var/lib/influxdb influxdb

Set the retention policy for Influx

    sudo docker exec -ti influxcontainer influx 
    > create database rtt
    > alter retention policy autogen on rtt duration 520w 


## Building the analytics module and running

Once you have all the dependencies installed and the data folder populated with the asn.db and ip2location-db5.bin files, you can build the analytics container..

    sudo docker build -t analytics .
    
To run it, you need to specify a few zmq sockets: sockets it uses to receive data and sockets it will set up to publish data on. In this command below we have IP addresses of two other containers: 172.17.0.2 is the frontend and 172.17.0.3 is the influx container. Change according to your IPs (that you can get with docker inspect).

    sudo docker run -d -p 5502:5502 -p 5503:5503 analytics ./analytics --bind tcp://0.0.0.0:5502 tcp://0.0.0.0:5503 --publish tcp://172.17.0.2:6080 --influx http://172.17.0.3:8086/write?db=rtt


If you see this output (have a look with docker logs), the analytics is running:

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

