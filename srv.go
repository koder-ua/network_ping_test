package main

import (
    "fmt"
    "net"
    "os"
    // "flag" can't make it work
    "time"
    "math/rand"
    "strconv"
)

const (
    CONN_HOST = "0.0.0.0"
    CONN_PORT = "33331"
    CONN_TYPE = "tcp"
)

var MESSAGE = []byte("Hello World!\n")

func handleRequest(conn net.Conn, msg_count int, timeout int, initTimeout int) {
    // fmt.Println(timeout, initTimeout)
    buf := make([]byte, 1024)
    if initTimeout > 0 {
        time.Sleep(time.Duration(initTimeout) * time.Millisecond)
    }

    for i := 0; i < msg_count; i++ {
        conn.Write(MESSAGE)
        if (timeout > 0) {
            time.Sleep(time.Duration(timeout) * time.Millisecond)
        }
        _, err := conn.Read(buf)
        if err != nil {
            break
        }
    }
    conn.Close()
}

func mainLoop(timeout int, msg_count int) {
    l, err := net.Listen(CONN_TYPE, CONN_HOST+":"+CONN_PORT)
    if err != nil {
        fmt.Println("Error listening:", err.Error())
        os.Exit(1)
    }
    defer l.Close()
    fmt.Println("Listening on " + CONN_HOST + ":" + CONN_PORT)
    randGen := rand.New(rand.NewSource(42))
    for {
        conn, err := l.Accept()
        if err != nil {
            fmt.Println("Error accepting: ", err.Error())
            os.Exit(1)
        }

        initTimeout := 0
        if timeout > 0 {
            initTimeout = randGen.Intn(timeout)
        }

        go handleRequest(conn, msg_count, timeout, initTimeout)
    }
}

func main() {
    if len(os.Args) != 3 {
        fmt.Println("Usage ", os.Args[0], " SEND_TIMEOUT MESSAGE_COUNT")
        os.Exit(1)
    }
    
    timeout, err1 := strconv.Atoi(os.Args[1])
    msg_count, err2 := strconv.Atoi(os.Args[2])
    if ((err1 != nil) || (err2 != nil)) {
        fmt.Println("Usage ", os.Args[0], " SEND_TIMEOUT MESSAGE_COUNT")
        os.Exit(1)
    }

    mainLoop(timeout, msg_count)
}

