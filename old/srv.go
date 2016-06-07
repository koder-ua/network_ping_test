package main

import (
	"flag"
	"log"
	"net"
	"strconv"
	"strings"
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

func NewSettings(sendTimeout, connectionTime, memorySize int) Settings {
	return Settings{
		SendTimeout:    sendTimeout,
		ConnectionTime: connectionTime,
		MemorySize:     memorySize,
	}
}

func handleRequest(conn *net.TCPConn, settings Settings, message []byte) {
	defer conn.Close()
	buf := make([]byte, settings.MemorySize)
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

func controlProc(settings chan Settings, statsChannel chan Stat) {
	log.Printf("Resolve UDP address on port %d", CONTROL_PORT)
	addr, _ := net.ResolveUDPAddr("udp", ":" + strconv.Itoa(CONTROL_PORT))
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

func mainLoop(timeout int, connectionTime int, msize int) {
	settingChannel := make(chan Settings)
	statsChannel := make(chan Stat)
	connectionChannel := make(chan net.TCPConn)
	currentSettings := NewSettings(timeout, connectionTime, msize)

	go controlProc(settingChannel, statsChannel)
	go masterProc(connectionChannel)

	message := []byte(strings.Repeat("X", msize))

	for {
		select {
		case conn := <-connectionChannel:
			log.Printf("Process connection %v", conn)
			go handleRequest(&conn, currentSettings, message)
		case newStat := <-statsChannel:
			// TODO: process statics.
			log.Printf("Gathered new statistic %v", newStat)
		case newSettings := <-settingChannel:
			log.Printf("Change settings to %v", newSettings)
			currentSettings = newSettings
		default:
			log.Println("No value ready, moving on.")
		}
	}
}

func main() {
	sendTimeout := *flag.Int("sendTimeout", 10, "Send timeout")
	connectionTime := *flag.Int("connectionTime", 600, "Connection time")
	memorySize := *flag.Int("memorySize", 1024, "Send timeout")

	mainLoop(sendTimeout, connectionTime, memorySize)
}
