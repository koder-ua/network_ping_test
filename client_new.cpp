#include <set>
#include <array>
#include <atomic>
#include <vector>
#include <cstdio>
#include <thread>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <functional>

#include <poll.h>
#include <fcntl.h>
#include <netdb.h>
#include <unistd.h>
#include <pthread.h>
#include <stropts.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <netinet/in.h>

class RSelector {
public:
    virtual bool add_fd(int sockfd) = 0;
    virtual void remove_current_ready() = 0;
    virtual bool wait() = 0;
    virtual bool next(int & sockfd, uint32_t & flags) = 0;
};

class FDList {
public:
    std::vector<int> fds;
    ~FDList() {
        for(int fd: fds)
            close(fd);
    }
};

class FDCloser {
public:
    int fd;
    FDCloser(int _fd): fd(_fd) {}
    ~FDCloser() { close(fd); }
};

class PollRSelector: public RSelector {
protected:
    std::vector<pollfd> fds;
    std::vector<pollfd>::iterator current_free;
    std::vector<pollfd>::iterator current_ready;

public:
    PollRSelector(int fd_count) {
        fds.resize(fd_count);
        current_free = fds.begin();
        current_ready = current_free;
        std::memset(&fds[0], 0, sizeof(fds[0]) * fd_count);
    }

    bool add_fd(int sockfd) {
        if (current_free == fds.end()) {
            std::cerr << "no space left in fd pool\n";
            return false;
        }
        current_free->fd = sockfd;
        current_free->events = POLLIN;
        ++current_free;
        return true;
    }

    bool wait() {
        int rv = poll(&fds[0], current_free - fds.begin(), -1);
        if (-1 == rv) {
            std::perror("poll(fds, ..., -1) fails");
            return false;
        }
        current_ready = fds.begin();
        return true;
    }

    bool next(int & sockfd, uint32_t & flags) {
        for(;fds.end() != current_ready; ++current_ready) {
            if (0 == current_ready->revents or current_ready->fd == -1)
                continue;
            sockfd = current_ready->fd;
            flags = current_ready->revents;
            ++current_ready;
            return true;
        }
        return false;
    }

    void remove_current_ready() {
        (current_ready - 1)->fd = -1;
    }
};

class EPollRSelector: public RSelector {
protected:
    int efd;
    std::vector<epoll_event> events;
    std::vector<epoll_event>::iterator current_ready;
    std::vector<epoll_event>::iterator end_of_ready;

public:
    EPollRSelector(int th_count) {
        efd = epoll_create1(0);
        if (-1 == efd) {
          perror("epoll_create");
        }
        events.resize(th_count);
    }

    ~EPollRSelector() { close(efd); }
    bool ok() const { return efd != -1;}

    bool add_fd(int sockfd) {
        epoll_event event;

        event.data.fd = sockfd;
        event.events = EPOLLIN | EPOLLET;
        if (-1 == epoll_ctl(efd, EPOLL_CTL_ADD, sockfd, &event)) {
            perror("epoll_ctl");
            return false;
        }
        return true;
    }
    
    bool wait() {
        int n = epoll_wait(efd, &events[0], events.size(), -1);

        if (-1 == n) {
            std::perror("epoll_wait fails");
            return false;
        }
        current_ready = events.begin();
        end_of_ready = current_ready + n;
        return true;
    }

    void remove_current_ready() {
        epoll_ctl(efd, EPOLL_CTL_DEL, (current_ready - 1)->data.fd, nullptr);
    }

    bool next(int & sockfd, uint32_t & flags) {
        if(end_of_ready == current_ready)
            return false;

        sockfd = current_ready->data.fd;
        flags = current_ready->events;
        ++current_ready;
        return true;
    }
};

const unsigned long NS_TO_S = 1000 * 1000 * 1000;
unsigned long time_ns()
{
    struct timespec spec;
    clock_gettime(CLOCK_REALTIME, &spec);
    return spec.tv_sec * NS_TO_S + spec.tv_nsec;
}


bool wait_for_conn(int sock_count,
                   std::vector<int> & sockets,
                   const char * ip,
                   const int port,
                   void (*ready_for_connect)(),
                   std::function<void(int)> * on_sock_cb,
                   bool async=false)
{
    (void)ip;

    sockaddr_in server, client;
    int master_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (-1 == master_sock){
        perror("Could not create socket");
        return false;
    }

    FDCloser _master_sock(master_sock);
    int enable = 1;
    if (setsockopt(master_sock, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable)) < 0)
        perror("setsockopt(SO_REUSEADDR) failed");

    server.sin_family = AF_INET;
    server.sin_addr.s_addr = INADDR_ANY;
    server.sin_port = htons(port);

    if( 0 > bind(master_sock, (sockaddr *)&server , sizeof(server))) {
        perror("bind failed. Error");
        return 1;
    }
     
    listen(master_sock, 100);
    socklen_t sock_data_len = sizeof(client);
    
    if (nullptr != ready_for_connect)
        ready_for_connect();

    for(int i = 0; i < sock_count; ++i){
        int client_sock = accept(master_sock, (sockaddr *)&client, &sock_data_len);
        if (client_sock < 0) {
            perror("accept failed");
            return false;
        }

        if(async) {
            int flags = fcntl(client_sock, F_GETFL, 0);
            if (flags < 0) { 
                std::perror("fcntl(client_sock, F_GETFL, 0)");
                return false;
            } 

            if (fcntl(client_sock, F_SETFL, flags | O_NONBLOCK) < 0) { 
                std::perror("fcntl(client_sock, F_SETFL, flags | O_NONBLOCK)");
                return false;
            }
        }
        sockets.push_back(client_sock);
        if (nullptr != on_sock_cb) {
            (*on_sock_cb)(client_sock);
        }
    }
    return true;
}

bool process_message(int sockfd, const char * message, int message_len) {
    char buffer[message_len];
    int bc = recv(sockfd, buffer, message_len, 0);
    if (0 > bc) {
        if (ECONNRESET != errno)
            std::perror("recv(sockfd, buffer.begin(), buffer.size(), 0)");
        return false;
    } else if (0 == bc) {
        return false;
    } else if (message_len != bc){
        std::perror("partial message");
        return false;
    }

    if (message_len != write(sockfd, message, message_len)) {
        std::perror("write(sockfd, message, std::strlen(message))");
        return false;   
    }

    return true;
}

void th_func(int sockfd, const char * message, int msize) {
    while(process_message(sockfd, message, msize));
}

extern "C"
int run_test_th(const char * ip,
                const int port,
                const int th_count,
                int msize,
                void (*ready_for_connect)(),
                void (*preparation_done)(),
                void (*test_done)())
{
    char message[msize];
    std::memset(message, 'X', msize);

    FDList sockets;
    std::vector<std::thread> threads;
    std::function<void(int)> cb = [&](int sock){
        threads.emplace_back(th_func, sock, &message[0], msize);
    };

    if (not wait_for_conn(th_count,
                          sockets.fds,
                          ip,
                          port,
                          ready_for_connect,
                          &cb,
                          false))
        return 1;

    if (nullptr != preparation_done)
        preparation_done();

    for(auto & th: threads)
        th.join();

    if (nullptr != test_done)
        test_done();

    return 0;
}

int run_test(RSelector & selector,
             const char * ip,
             const int port,
             const int th_count,
             const int msize,
             void (*ready_for_connect)(),
             void (*preparation_done)(),
             void (*test_done)())
{
    int fd_left = th_count;
    char message[msize];
    std::memset(message, 'X', msize);
    FDList sockets;

    if (not wait_for_conn(th_count, sockets.fds, ip, port, ready_for_connect, nullptr, false))
        return 1;

    if (nullptr != preparation_done)
        preparation_done();

    for(int sockfd: sockets.fds)
        if (not selector.add_fd(sockfd))
            return 1;

    while(fd_left > 0) {
        if (not selector.wait())
            return 1;

        uint32_t events;
        int sockfd;
        while(selector.next(sockfd, events)) {
            bool close_sock = false;

            if ((events & POLLHUP) or (events & POLLERR)) {
                close_sock = true;
            } else if (events & POLLNVAL) {
                std::cerr << "Poll - POLLNVAL for fd " << sockfd;
                std::cerr << " val " << events << "\n";
                close_sock = true;
            } else if (events & POLLIN) {
                close_sock = not process_message(sockfd, message, msize);
            } else if (0 != events) {
                std::cerr << "Poll - ??? for fd " << sockfd;
                std::cerr << " val " << events << "\n";
                close_sock = true;
            }

            if (close_sock) {
                selector.remove_current_ready();
                --fd_left;
            }
        }
    }

    if (nullptr != test_done)
        test_done();

    return 0;
}

extern "C"
int run_test_epoll(const char * ip,
                   const int port,
                   const int th_count,
                   int msize,
                   void (*ready_for_connect)(),
                   void (*preparation_done)(),
                   void (*test_done)())
{
    EPollRSelector eps(th_count);
    if (not eps.ok())
        return 1;
    return run_test(eps, ip, port, th_count, msize, ready_for_connect, preparation_done, test_done);
}

extern "C"
int run_test_poll(const char * ip,
                  const int port,
                  const int th_count,
                  int msize,
                  void (*ready_for_connect)(),
                  void (*preparation_done)(),
                  void (*test_done)())
{
    PollRSelector eps(th_count);
    return run_test(eps, ip, port, th_count, msize, ready_for_connect, preparation_done, test_done);
}
