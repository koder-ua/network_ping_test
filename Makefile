#!/bin/bash
.PHONY: clean rebuild all

BIN_FOLDER:=bin
BINARIES:=$(BIN_FOLDER)/libclient.so $(BIN_FOLDER)/server_cpp

WITH_RDTSC:=-DUSERDTSC

CPP_OPTS:=-pthread -Wall -Wpedantic -Wno-vla -Wextra -std=c++11
CPP_PROF:=-O2 -pg -march=native
VTUNE_CPP_PROF:=-O2 -march=native
CPP_O3:=-O3 -march=native -fomit-frame-pointer
CPP_DEBUG:=-O0 -fno-omit-frame-pointer -g3 -ggdb
CPP_SHARED:=-shared -fPIC

CPP_OPTS:=$(CPP_OPTS) $(CPP_O3)

COMPILER=g++

all: $(BINARIES)

$(BIN_FOLDER)/server_cpp: server.cpp Makefile	
		$(COMPILER) $(CPP_OPTS) $< -o $@

$(BIN_FOLDER)/libclient.so: client.cpp Makefile
		$(COMPILER) $(CPP_OPTS) $(CPP_SHARED) -DBUILDSHARED $< -o $@ 

clean:
		rm -f $(BINARIES)

rebuild: clean all
