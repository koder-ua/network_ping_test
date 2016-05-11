#!/bin/bash
.PHONY: clean rebuild all

BIN_FOLDER=bin
BINARIES = $(BIN_FOLDER)/client_cpp $(BIN_FOLDER)/libclient.so $(BIN_FOLDER)/libclient2.so $(BIN_FOLDER)/server_cpp

# CPP_OPTS = -pthread -O3 -march=native -Wall -Wpedantic -Wno-vla -Wextra -std=c++11
CPP_OPTS = -pthread -O0 -Wall -Wpedantic -Wno-vla -Wextra -std=c++11 -g -ggdb

all: $(BINARIES)

$(BIN_FOLDER)/client_cpp: client.cpp Makefile	
		g++ $(CPP_OPTS) client.cpp -o $@

$(BIN_FOLDER)/server_cpp: server.cpp Makefile	
		g++ $(CPP_OPTS) server.cpp -o $@

$(BIN_FOLDER)/libclient.so: client.cpp Makefile
		g++ $(CPP_OPTS) -DBUILDSHARED -shared -fPIC client.cpp -o $@ 

$(BIN_FOLDER)/libclient2.so: client_new.cpp Makefile
		g++ $(CPP_OPTS) -DBUILDSHARED -shared -fPIC client_new.cpp -o $@ 

# srv: srv.go Makefile
# 		go build srv.go

clean:
		rm -f $(BINARIES)

rebuild: clean all
