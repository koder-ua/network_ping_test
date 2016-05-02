#!/bin/bash
.PHONY: clean rebuild all

BINARIES = client_cpp libclient.so srv
CPP_OPTS = -pthread -O3 -march=native -Wall -Wpedantic -Wno-vla -Wextra -std=c++11
# CPP_OPTS = -pthread -O0 -Wall -Wpedantic -Wno-vla -Wextra -std=c++11 -g -ggdb

all: $(BINARIES)

client_cpp: client.cpp Makefile	
		g++ $(CPP_OPTS) client.cpp -o $@

libclient.so: client.cpp Makefile	
		g++ $(CPP_OPTS) -DBUILDSHARED -shared -fPIC client.cpp -o $@ 

srv: srv.go Makefile
		go build srv.go

clean:
		rm -f $(BINARIES)

rebuild: clean all
