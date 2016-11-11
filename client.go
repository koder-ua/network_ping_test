package main

// These wrappers are made over unsafe.Pointer received from Python to be callable in Go

// #cgo pkg-config: python3
// #define Py_LIMITED_API
// #include <Python.h>
//extern void BeforeTest();
//static inline void before_test(void* f) {
//    void (*func)() = f;
//    func();
//}
//extern void AfterTest();
//static inline void after_test(void* f) {
//    void (*func)() = f;
//    func();
//}
//extern void ReadyConn();
//static inline void ready_conn(void* f) {
//    void (*func)() = f;
//    func();
//}
import "C"

import (
	"fmt"
	"log"
	"net"
	"os"
	"sync"
	"unsafe"
)

var (
	Info *log.Logger
	Err  *log.Logger
)

func init() {
	Info = log.New(os.Stdout,
		"INFO: ",
		log.Ldate|log.Ltime|log.Lshortfile)

	Err = log.New(os.Stderr,
		"ERROR: ",
		log.Ldate|log.Ltime|log.Lshortfile)
}

type Config struct {
	LocalAddr       string
	LocalPort       int
	ConnectionCount int
	BlockSize       int
	// NOTE: Can't get purpose of this parameter
	ListenQueue int
	BeforeTest  unsafe.Pointer
	AfterTest   unsafe.Pointer
	ReadyConn   unsafe.Pointer
	sync.WaitGroup
}

var config Config

// Golang wrappers around CGO wrappers around python callbacks.
func beforeTest(f unsafe.Pointer) {
	Info.Printf("Before test hook")
	C.before_test(f)
}

func afterTest(f unsafe.Pointer) {
	Info.Printf("After test hook")
	C.before_test(f)
}

func readyConn(f unsafe.Pointer) {
	Info.Printf("Connections are ready")
	C.ready_conn(f)
}

func newConfig(localAddr string, localPort, connectionCount, msize, listenQueue int,
	before, after, readyConn unsafe.Pointer) Config {
	return Config{
		LocalAddr:       localAddr,
		LocalPort:       localPort,
		ConnectionCount: connectionCount,
		BlockSize:       msize,
		ListenQueue:     listenQueue,
		BeforeTest:      before,
		AfterTest:       after,
		ReadyConn:       readyConn,
	}
}

//export RunTest
func RunTest(h *C.char, port, threadCount, msize, listenQueue int,
	readyConn, before, after unsafe.Pointer) int {
	localAddr := C.GoString(h)
	Info.Printf("Start Test")
	Info.Printf("LocalAddr: %s LocalPort: %d ThreadCount: %d Msize: %d ListenQueue: %d",
		localAddr, port, threadCount, msize, listenQueue)
	config = newConfig(localAddr, port, threadCount, msize, listenQueue, before, after, readyConn)
	Info.Printf("Start load test on %s:%d", config.LocalAddr, config.LocalPort)
	// Do load test
	doLoad()
	Info.Printf("Test is finished")

	return 0
}

// Create tcp server on localAddr:localPort and waits for count of
// connection to be established.
func doLoad() {
	servAddr := fmt.Sprintf("%s:%d", config.LocalAddr, config.LocalPort)
	tcpAddr, _ := net.ResolveTCPAddr("tcp", servAddr)
	masterSocket, err := net.ListenTCP("tcp", tcpAddr)

	if err != nil {
		Err.Printf("Error while opening master socket %s", err.Error())
	} else {
		Info.Printf("Connected to server %s", servAddr)
	}

	// Signal server to open connections to the client
	readyConn(config.ReadyConn)
	// Prepare all connections
	prepare(masterSocket)
	masterSocket.Close()
	Info.Printf("Connections are prepared, master socket is closed.")
	// Save time spent on test
	beforeTest(config.BeforeTest)
	// Wait until all workers are finished
	Info.Printf("Test is going on")
	config.Wait()
	afterTest(config.AfterTest)
}

// Wait for count connections to be established
func prepare(masterSocket *net.TCPListener) {
	Info.Printf("Prepare connections")
	for i := 0; i < config.ConnectionCount; i++ {
		conn, err := masterSocket.Accept()

		if err != nil {
			Err.Printf("Error while accepting connection from server")
		}
		config.Add(1)
		go worker(conn)
	}
}

func worker(conn net.Conn) {
	// Allocate bytes buffer before start test
	buffer := make([]byte, config.BlockSize)
	defer conn.Close()
	defer config.Done()

	for {
		count, err := conn.Read(buffer)

		// Usually used for stopping worker by tcp reset
		if err != nil {
			Err.Printf("Error while reading from socket %s",
				err.Error())
			break
		}

		if count == 0 {
			break
		} else if count < config.BlockSize {
			Err.Printf("Partial message")
		}
		conn.Write(buffer)
	}
}

func main() {

}
