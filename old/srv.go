package main

import (
	"flag"
	"log"
	"net"
	"strconv"
	"strings"
	"sync"
	"time"
)

const (
	CONN_HOST    = "0.0.0.0"
	CONN_PORT    = 33331
	CONTROL_PORT = 33332
	CONN_TYPE    = "tcp"
)

type Settings struct {
	SendTimeout    int
	ConnectionTime int
	MemorySize     int
}

type Stat struct {
	// TODO: Some statistical staff goes here.
	Mean         float64
	Average      float64
	Median       float64
	Percentile90 float64
	Percentile95 float64
	Percentile99 float64
	Count        int64
}

var (
	pool            sync.Pool
	currentSettings Settings
	message         []byte
)

func NewSettings(sendTimeout, connectionTime, memorySize int) Settings {
	return Settings{
		SendTimeout:    sendTimeout,
		ConnectionTime: connectionTime,
		MemorySize:     memorySize,
	}
}

func acceptorProc(connectionChannel chan net.TCPConn, done chan bool) {
	for {
		select {
		case conn := <-connectionChannel:
			log.Printf("Process connection %v", conn)
			go handleRequest(&conn, currentSettings, message)
		case <-done:
			// TODO: do final things.
			return
		}
	}
}

func handleRequest(conn *net.TCPConn, settings Settings, message []byte) {
	defer conn.Close()
	// Get byte slice from pool. And release it to pool in defer .
	buf := pool.Get().([]byte)
	defer pool.Put(buf)

	etime := int64(0)

	for etime == 0 || time.Now().UnixNano()/1000000 < etime {
		conn.Write(message)
		if settings.SendTimeout > 0 {
			time.Sleep(time.Duration(settings.SendTimeout) * time.Millisecond)
		}
		_, err := conn.Read(buf)
		if err != nil {
			break
		}
		if etime == 0 {
			etime = time.Now().UnixNano()/1000000 + int64(settings.ConnectionTime)
		}
	}
}

func controlProc(settings chan Settings, statsChannel chan Stat, done, termination chan bool) {
	log.Printf("Resolve UDP address on port %d", CONTROL_PORT)
	addr, _ := net.ResolveUDPAddr("udp", ":"+strconv.Itoa(CONTROL_PORT))
	log.Printf("Connecting to UDP on addr %v", *addr)
	conn, err := net.ListenUDP("udp", addr)
	defer conn.Close()

	if err != nil {
		log.Printf("Error %s", err.Error())
	} else {
		log.Printf("Connection on %v was established successfully from controlProc", *addr)
	}

	buffer := make([]byte, 1024)

	for {
		n, addr, err := conn.ReadFromUDP(buffer)

		if err != nil {
			log.Printf("Error has been occured %s", err.Error())
		}

		s := strings.SplitN(string(buffer[:n]), " ", 2)
		if len(s) == 1 {
		}
		log.Printf("Received %d bytes from %s", n, string(addr.IP))

		// TODO: send reply here
	}
}

func masterProc(connectionChannel chan net.TCPConn) {
	defer close(connectionChannel)

	log.Printf("Connecting to %s : %d ", CONN_HOST, CONN_PORT)
	addr := net.TCPAddr{
		IP:   net.ParseIP(CONN_HOST),
		Port: CONN_PORT,
		Zone: ""}

	l, err := net.ListenTCP(CONN_TYPE, &addr)

	if err != nil {
		log.Fatal("Error ", err.Error())
	} else {
		log.Printf("Connection in %v was established successfully from masterProc", addr)
	}

	for {
		conn, err := l.AcceptTCP()

		if err != nil {
			log.Fatal("Error accepting: ", err.Error())
		}

		err = conn.SetNoDelay(true)

		if err != nil {
			log.Printf("Error while setting listener No Delay True %s", err.Error())
		}

		connectionChannel <- *conn
	}
}

func processStats(statsChannel chan Stat, done chan bool) {
	for {
		select {
		case stat := <- statsChannel:
			log.Printf("Receive stats %v", stat)
		case <-done:
			// TODO: do final thing before exit.
			return
		}
	}
}

func mainLoop(timeout int, connectionTime int, msize int) {
	settingChannel := make(chan Settings)
	statsChannel := make(chan Stat)
	done := make(chan bool)
	termination := make(chan bool)

	connectionChannel := make(chan net.TCPConn)
	currentSettings = NewSettings(timeout, connectionTime, msize)
	message = []byte(strings.Repeat("X", msize))

	// Creating Pool object for storing byte slices.
	pool = sync.Pool{
		New: func() interface{} {
			return make([]byte, currentSettings.MemorySize)
		},
	}

	go controlProc(settingChannel, statsChannel, done, termination)
	go masterProc(connectionChannel)
	go processStats(statsChannel, done)
	go acceptorProc(connectionChannel, done)

	<-termination
}

func main() {
	sendTimeout := *flag.Int("sendTimeout", 10, "Send timeout")
	connectionTime := *flag.Int("connectionTime", 600, "Connection time")
	memorySize := *flag.Int("memorySize", 1024, "Send timeout")

	mainLoop(sendTimeout, connectionTime, memorySize)
}
