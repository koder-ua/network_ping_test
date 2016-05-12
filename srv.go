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
    CONTROL_PORT = 33332
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

struct Settings {}
struct Stat {}

func controlProc(req_ch chan Stat) {
    addr, err := net.ResolveUDPAddr("udp", ":" + str(CONTROL_PORT))
    conn, err := net.ListenUDP("udp", &addr)
    defer conn.Close()
    buffer := make([]byte, 1024)

    for {
        n, addr, err := conn.ReadFromUDP(buffer)
        s := strings.SplitN(string(buffer[:n]), " ", 2)
        if len(s) == 1 {}

        // send reply
    }
}

func masterProc(conn_ch chan net.TCPConn) {
    addr := net.TCPAddr{net.ParseIP(CONN_HOST), CONN_PORT, ""}
    l, err := net.ListenTCP(CONN_TYPE, &addr)
    if err != nil {
        fmt.Println("Error listening:", err.Error())
        os.Exit(1)
    }

    for {
        conn, err := l.AcceptTCP()
        if err != nil {
            fmt.Println("Error accepting: ", err.Error())
            os.Exit(1)
        }
        l = conn.SetNoDelay(true)
        conn_ch <- l
    }
}

func mainLoop(timeout int, conn_time int, msize int) {

    settings_chan := make chan Settings
    stat_chan := make chan Stat
    conn_chan := make chan net.TCPConn

    go controlProc(settings_chan, stat_chan)
    go masterProc()

    message := []byte(strings.Repeat("X", msize))

    addr := net.TCPAddr{net.ParseIP(CONN_HOST), CONN_PORT, ""}
    l, err := net.ListenTCP(CONN_TYPE, &addr)
    if err != nil {
        fmt.Println("Error listening:", err.Error())
        os.Exit(1)
    }
    for {
        select {
        case conn := <-conn_chan:
            go handleRequest(conn, curr_sett, message)
        case new_stat := <-stat_chan:
            ...
        case addr, req := <-req_chan:
            ...
        default:
            fmt.Println("No value ready, moving on.")
        }                
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

