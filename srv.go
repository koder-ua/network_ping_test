package main

import (
    "fmt"
    "net"
    "os"
    "time"
    "strconv"
    "strings"
)

const (
    CONN_HOST = "0.0.0.0"
    CONN_PORT = 33331
    CONN_TYPE = "tcp"
)


func handleRequest(conn *net.TCPConn, max_time int, timeout int, msize int, message []byte) {
    buf := make([]byte, msize)
    etime := int64(0)

    for etime == 0 || time.Now().UnixNano() / 1000000 < etime {
        conn.Write(message)
        if (timeout > 0) {
            time.Sleep(time.Duration(timeout) * time.Millisecond)
        }
        _, err := conn.Read(buf)
        if err != nil {
            break
        }
        if etime == 0 {
            etime = time.Now().UnixNano() / 1000000 + int64(max_time)
        }
    }
    conn.Close()
}

func mainLoop(timeout int, conn_time int, msize int) {
    message := []byte(strings.Repeat("X", msize))

    addr := net.TCPAddr{net.ParseIP(CONN_HOST), CONN_PORT, ""}
    l, err := net.ListenTCP(CONN_TYPE, &addr)
    if err != nil {
        fmt.Println("Error listening:", err.Error())
        os.Exit(1)
    }
    defer l.Close()
    for {
        conn, err := l.AcceptTCP()
        if err != nil {
            fmt.Println("Error accepting: ", err.Error())
            os.Exit(1)
        }

        _ = conn.SetNoDelay(true)
        go handleRequest(conn, conn_time, timeout, msize, message)
    }
}

func main() {
    if len(os.Args) != 4 {
        fmt.Println("Usage ", os.Args[0], " SEND_TIMEOUT CONN_USE_TIME_MS RSIZE")
        os.Exit(1)
    }
    
    timeout, err1 := strconv.Atoi(os.Args[1])
    conn_time, err2 := strconv.Atoi(os.Args[2])
    msize, err3 := strconv.Atoi(os.Args[3])

    if ((err1 != nil) || (err2 != nil) || (err3 != nil)) {
        fmt.Println("Usage ", os.Args[0], " SEND_TIMEOUT CONN_USE_TIME_MS RSIZE")
        os.Exit(1)
    }

    mainLoop(timeout, conn_time, msize)
}

