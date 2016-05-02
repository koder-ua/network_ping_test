#include <set>
#include <array>
#include <mutex>
#include <atomic>
#include <vector>
#include <cstdio>
#include <thread>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <condition_variable>

#include <poll.h>
#include <fcntl.h>
#include <netdb.h>
#include <unistd.h>
#include <stropts.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <netinet/in.h>

const char message[] = "Hello World!\n";
const int message_len = sizeof(message) - 1;

bool connect_all(int sock_count,
                 std::vector<int> & sockets,
                 const char * ip,
                 const int port,
                 bool async=false)
{
    const struct hostent * host = gethostbyname(ip);
    if (NULL == host) {
        std::perror("No such host");
        return 0;
    }

    struct sockaddr_in serv_addr;
    bzero((char *)&serv_addr, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    bcopy((const char *)host->h_addr, (char *)&serv_addr.sin_addr.s_addr, host->h_length);
    serv_addr.sin_port = htons(port);

    sockets.clear();
    for(int i = 0; i < sock_count ; ++i) {
        int sockfd = socket(AF_INET, SOCK_STREAM, 0);
        if (sockfd < 0) {
            std::perror("Socket creation");
            return false;
        }

        if (0 > connect(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr))) {
            std::perror("Connecting:");
            return false;
        }

        if(async) {
            int flags = fcntl(sockfd, F_GETFL, 0);
            if (flags < 0) { 
                std::perror("fcntl(sockfd, F_GETFL, 0)");
                return false;
            } 

            if (fcntl(sockfd, F_SETFL, flags | O_NONBLOCK) < 0) { 
                std::perror("fcntl(sockfd, F_SETFL, flags | O_NONBLOCK)");
                return false;
            }
        }
        sockets.push_back(sockfd);
    }
    return true;
}

bool process_message(int sockfd) {
    std::array<char, 32> buffer;
    int bc = recv(sockfd, buffer.begin(), buffer.size(), 0);
    if (0 > bc) {
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


class RSelector {
public:
    virtual bool add_fd(int sockfd) = 0;
    virtual void remove_current_ready() = 0;
    virtual bool wait() = 0;
    virtual bool next(int & sockfd, uint32_t & flags) = 0;
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


void th_func(int sockfd,
             std::atomic_ulong * msg_processed_a){
    int counter = 0;
    while(process_message(sockfd))
        ++counter;
    *msg_processed_a += counter;
}


extern "C"
int run_test_th(const char * ip, const int port,
                const int th_count, int * msg_processed,
                void (*preparation_done)(), void (*test_done)()) {
    std::atomic_ulong counter{0};
    std::vector<int> sockets;
    std::thread threads[th_count];

    if (not connect_all(th_count, sockets, ip, port, false)) {
        for(int sockfd: sockets)
            close(sockfd);
        return 1;
    }

    for(unsigned int idx = 0; idx < sockets.size(); ++idx)
        threads[idx] = std::thread(th_func, sockets[idx], &counter);

    if (nullptr != preparation_done)
        preparation_done();

    for(auto & th: threads)
        th.join();

    if (nullptr != test_done)
        test_done();

    if (nullptr != msg_processed)
        *msg_processed = counter.load();

    for(int sockfd: sockets)
        close(sockfd);    

    return 0;
}

int run_test(RSelector & selector,
             const char * ip, const int port, const int th_count, int * msg_processed,
             void (*preparation_done)(), void (*test_done)()) {
    int counter = 0;
    int fd_left = th_count;
    std::vector<int> sockets;

    if (not connect_all(th_count, sockets, ip, port, true)) {
        for(int sockfd: sockets)
            close(sockfd);
        return 1;
    }

    if (nullptr != preparation_done)
        preparation_done();

    for(int sockfd: sockets) {
        if (not selector.add_fd(sockfd)) {
            for(int sockfd: sockets)
                close(sockfd);
            return 1;
        }
    }

    while(fd_left > 0) {
        if (not selector.wait()){
            for(int sockfd: sockets)
                close(sockfd);
            return 1;
        }

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
                close_sock = not process_message(sockfd);
                if (not close_sock)
                    counter += 1;
            } else if (0 != events) {
                std::cerr << "Poll - ??? for fd " << sockfd;
                std::cerr << " val " << events << "\n";
                close_sock = true;
            }

            if (close_sock) {
                close(sockfd);
                selector.remove_current_ready();
                --fd_left;
            }
        }
    }

    if (nullptr != test_done)
        test_done();

    if (nullptr != msg_processed)
        *msg_processed = counter;

    for(int sockfd: sockets)
        close(sockfd);

    return 0;
}

extern "C"
int run_test_epoll(const char * ip, const int port, const int th_count, int * msg_processed,
                   void (*preparation_done)(), void (*test_done)()) {
    EPollRSelector eps(th_count);
    if (not eps.ok()) {
        return 1;
    }
    return run_test(eps, ip, port, th_count, msg_processed, preparation_done, test_done);
}

extern "C"
int run_test_poll(const char * ip, const int port, const int th_count, int * msg_processed,
                  void (*preparation_done)(), void (*test_done)()) {
    PollRSelector eps(th_count);
    return run_test(eps, ip, port, th_count, msg_processed, preparation_done, test_done);
}

#if !defined(BUILDSHARED)
int main(int argc, const char ** argv) {
    if (argc < 3) {
        std::cerr << "Usage " << argv[0] << " IP CONN_COUNT\n";
        return 1;
    }

    const char * ip = argv[1];
    const int th_count = std::atoi(argv[2]);
    const int port = 33331;

    if (0 == th_count) {
        std::cerr << "Usage " << argv[0] << " IP CONN_COUNT\n";
        return 1;
    }

    int msg_count = 0;

    // int err = run_test_poll(ip, port, th_count, &msg_count, nullptr, nullptr);
    // int err = run_test_epoll(ip, port, th_count, &msg_count, nullptr, nullptr);
    int err = run_test_th(ip, port, th_count, &msg_count, nullptr, nullptr);

    std::cout << msg_count << " message cycles processed\n";
    return err;
}
#endif
