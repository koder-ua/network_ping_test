#ifndef COMMON_H__
#define COMMON_H__
#include <vector>

#include <sys/epoll.h>

#define MICRO (1000 * 1000)
#define BILLION (1000 * 1000 * 1000)

struct EventsList {
    std::vector<epoll_event> events;
    int num_ready;
    unsigned long recv_time;
};

class RSelector {
public:
    virtual bool add_fd(int sockfd) = 0;
    virtual void remove_current_ready() = 0;
    virtual bool wait(long int timeout_ns=-1) = 0;
    virtual bool next(int & sockfd, uint32_t & flags) = 0;
};

class EPollRSelector: public RSelector {
protected:
    int efd;
    EventsList events;
    std::vector<epoll_event>::iterator current_ready;
    std::vector<epoll_event>::iterator end_of_ready;

#ifdef EPOLL_CALL_STATS
    unsigned long int sock_activation_count;
    unsigned int wait_count;
#endif

private:
  EPollRSelector();
  EPollRSelector(const EPollRSelector &);

public:
    EPollRSelector(int sock_count);
    EPollRSelector(EPollRSelector && rsel);
    ~EPollRSelector();
    bool ok() const {return efd != -1;}
    bool add_fd(int sockfd) {
        return add_fd(sockfd, EPOLLIN | EPOLLET);
    }

    bool add_fd(int sockfd, int events);
    bool wait(long int timeout_ns=-1);
    void remove_current_ready();
    int ready_count() const;
    bool next(int & sockfd, uint32_t & flags);
    bool next(int & sockfd);
};

// epoll_wait support timeout only with ms granularity
// while we need at least us presicion
bool epoll_wait_ex(int epollfd,
                   EventsList & ready,
                   long int timeout_ns);

inline unsigned long get_fast_time() {
   timespec curr_time;
   if( -1 == clock_gettime( CLOCK_REALTIME, &curr_time)) {
     perror( "clock gettime" );
     return 0;
   }

   return curr_time.tv_nsec + ((unsigned long)curr_time.tv_sec) * BILLION;

   // using namespace std::chrono;
   // auto curr_time = high_resolution_clock::now().time_since_epoch();
   // return (unsigned long) duration_cast<nanoseconds>(curr_time).count();
}

#endif //COMMON_H__
