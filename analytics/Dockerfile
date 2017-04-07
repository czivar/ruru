FROM ubuntu:14.04

RUN mkdir -p /root
WORKDIR /root

COPY . /root

RUN apt-get update
RUN apt-get install -y libtool pkg-config build-essential autoconf automake sqlite3 libsqlite3-dev libcurl4-gnutls-dev libzmq3-dev

WORKDIR /root/deps/IP2Location-C-Library
RUN autoreconf -i -v --force
RUN ./configure
RUN sudo make install
RUN sudo ldconfig

WORKDIR /root/deps/json-c
RUN ./autogen.sh
RUN ./configure
RUN sudo make install
RUN sudo ldconfig

WORKDIR /root/
RUN make

CMD ./analytics

