#!/bin/bash
.PHONY: clean rebuild all

BIN_FOLDER=bin
BINARIES=$(BIN_FOLDER)/libclient.so $(BIN_FOLDER)/server_cpp

CPP_OPTS_O3=-pthread -O3 -march=native -Wall -Wpedantic -Wno-vla -Wextra -std=c++11  -fomit-frame-pointer
CPP_OPTS_TEST=-pthread -O2 -Wall -Wpedantic -Wno-vla -Wextra -std=c++11  -fno-omit-frame-pointer -g -ggdb
CPP_OPTS=CPP_OPTS_TEST
# CPP_OPTS=-fsanitize=address -pthread -O0 -Wall -Wpedantic -Wno-vla -Wextra -std=c++11 -g3 -ggdb -fno-omit-frame-pointer
# CPP_OPTS=-pthread -O0 -Wall -Wpedantic -Wno-vla -Wextra -std=c++11 -g3 -ggdb -fno-omit-frame-pointer

all: $(BINARIES)

# $(BIN_FOLDER)/client_cpp: client.cpp Makefile	
# 		g++ $(CPP_OPTS) client.cpp -o $@

# $(BIN_FOLDER)/libclient.so: client.cpp Makefile
# 		g++ $(CPP_OPTS) -DBUILDSHARED -shared -fPIC client.cpp -o $@ 

# srv: srv.go Makefile
# 		go build srv.go

$(BIN_FOLDER)/server_cpp: server.cpp Makefile	
		g++ $(CPP_OPTS) $< -o $@

$(BIN_FOLDER)/libclient.so: client.cpp Makefile
		g++ $(CPP_OPTS) -DBUILDSHARED -shared -fPIC $< -o $@ 

clean:
		rm -f $(BINARIES)

rebuild: clean all
