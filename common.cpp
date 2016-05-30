#include <cstdio>
#include <iostream>

#include <time.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <sys/socket.h>

#include "common.h"

EPollRSelector::EPollRSelector(int sock_count) {
#ifdef EPOLL_CALL_STATS
    sock_activation_count = 0;
    wait_count = 0;
#endif
    efd = epoll_create1(0);
    if (-1 == efd) {
        perror("epoll_create");
    }
    events.events.resize(sock_count);
    current_ready = end_of_ready = events.events.begin();
}

EPollRSelector::EPollRSelector(EPollRSelector && rsel) {
    efd = rsel.efd;
    rsel.efd = -1;

    #ifdef EPOLL_CALL_STATS
    sock_activation_count = rsel.sock_activation_count;
    wait_count = rsel.wait_count;
    #endif

    current_ready = end_of_ready = events.events.begin();
}

EPollRSelector::~EPollRSelector() {
#ifdef EPOLL_CALL_STATS
    if (0 != wait_count) {
        std::cout << "Avg socket per wait = ";
        std::cout << sock_activation_count / wait_count << "\n";
    }
#endif
    if (-1 != efd)
        close(efd);
}

bool EPollRSelector::add_fd(int sockfd, int event_mask) {
    epoll_event event;

    event.data.fd = sockfd;
    event.events = event_mask;
    if (-1 == epoll_ctl(efd, EPOLL_CTL_ADD, sockfd, &event)) {
        perror("epoll_ctl");
        return false;
    }
    return true;
}

bool EPollRSelector::wait(long int timeout_ns) {
    if (not epoll_wait_ex(efd, events, timeout_ns))
        return false;

    current_ready = events.events.begin();
    end_of_ready = current_ready + events.num_ready;

#ifdef EPOLL_CALL_STATS
    wait_count += 1;
    sock_activation_count += n;
#endif
    return true;
}

bool EPollRSelector::next(int & sockfd, uint32_t & flags) {
    if(end_of_ready == current_ready)
        return false;

    sockfd = current_ready->data.fd;
    flags = current_ready->events;
    ++current_ready;
    return true;
}

bool EPollRSelector::next(int & sockfd) {
    if(end_of_ready == current_ready)
        return false;

    sockfd = current_ready->data.fd;
    ++current_ready;
    return true;
}

void EPollRSelector::remove_current_ready() {
    epoll_ctl(efd, EPOLL_CTL_DEL, (current_ready - 1)->data.fd, nullptr);
}

int EPollRSelector::ready_count() const {
    return end_of_ready - current_ready;
}

// epoll_wait support timeout only with ms granularity
// while we need at least us presicion
bool epoll_wait_ex(int epollfd,
                   EventsList & ready,
                   const long int timeout_ns)
{
    bool already_polled = false;

    ready.num_ready = 0;

    auto curr_time = get_fast_time();
    auto return_time = curr_time + timeout_ns;

    for(;;) {
        auto time_left = (long int)return_time - (long int)curr_time;

        if (time_left <= 0 && timeout_ns != -1) {
            if (already_polled)
                return true;
            else
                time_left = 0;
        }

        auto poll_timeout = (timeout_ns == -1 ? -1 : time_left / 1000000);

        ready.num_ready = epoll_wait(epollfd,
                                     &(ready.events[0]),
                                     ready.events.size(),
                                     poll_timeout);
        already_polled = true;

        curr_time = get_fast_time();

        if (ready.num_ready == 0) {
            if (curr_time >= return_time)
                return true;
            continue;
        } else if ( 0 > ready.num_ready ) {
            if (errno == EINTR) {
                ready.num_ready = 0;
                continue;
            } else {
                perror("epoll_wait failed");
                return false;
            }
        }

        #ifdef EPOLL_CALL_STATS
        if (0 != ready.num_ready) {
            socket_count_from_wait += ready.num_ready;
            epoll_wait_calls += 1;
        }
        #endif

        ready.recv_time = curr_time;
        return true;
    }
}
