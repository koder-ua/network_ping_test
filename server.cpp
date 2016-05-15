#include <map>
#include <queue>
#include <mutex>
#include <atomic>
#include <vector>
#include <thread>
#include <cstring>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <unordered_map>
#include <condition_variable>

#include <poll.h>
#include <fcntl.h>
#include <netdb.h>
#include <signal.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <netinet/in.h>


const int DEFAULT_PORT = 33331;
const int MAX_CLIENT_MESSAGE = 1024;
const int MICRO = 1000 * 1000;
const unsigned long BILLION = 1000 * 1000 * 1000;

struct TestParams{
    int port, num_conn, runtime, message_len;
    unsigned long int timeout;
    char ip[MAX_CLIENT_MESSAGE + 1];
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
    // FDCloser(int _fd): fd(_fd) {}
    ~FDCloser() {
        close(fd);
    }
};

template <typename T> class Queue {
private:
    std::queue<T> queue;
    std::mutex mutex;
    std::condition_variable cond;
public:
    T pop() {
        std::unique_lock<std::mutex> mlock(mutex);

        while (queue.empty())
            cond.wait(mlock);
        
        auto item = queue.front();
        queue.pop();
        return item;
    }
 
    void push(const T& item) {
        std::unique_lock<std::mutex> mlock(mutex);
        queue.push(item);
        mlock.unlock();
        cond.notify_one();
    }
 };

struct EventsList {
    std::vector<epoll_event> events;
    int num_ready;
    unsigned long recv_time;
};

std::vector<epoll_event>::iterator begin(EventsList & elist) {
    return elist.events.begin();
}

std::vector<epoll_event>::iterator end(EventsList & elist) {
    return elist.events.begin() + elist.num_ready;
}

struct TestResult{
    unsigned long int mcount;
    std::array<unsigned long int, 30> lat_ns_log2;
};

std::string serialize_to_str(const TestResult & res) {
    std::stringstream serialized;
    serialized << res.mcount;
    for(auto val: res.lat_ns_log2)
        serialized << " " << val;
    return serialized.str();
}

bool load_from_str(const char * data, TestParams & params) {
    if (std::strlen(data) > sizeof(params.ip)) {
        std::cerr << "Message too large\n";
        return false;
    }
    int num_scanned = std::sscanf(data, "%s %d %d %d %lu %d",
                                  params.ip,
                                  &params.port,
                                  &params.num_conn,
                                  &params.runtime,
                                  &params.timeout,
                                  &params.message_len);
    if (num_scanned != 6) {
        std::cerr << "Message from client is broken '" << data << "'\n";
        return false;
    }
    return true;
}

#ifdef USERDTSC
inline unsigned long gettime_helper() {
#else
inline unsigned long get_fast_time() {
#endif
    timespec curr_time;
    if( -1 == clock_gettime( CLOCK_REALTIME, &curr_time)) {
      perror( "clock gettime" );
      return 0;
    }

    return curr_time.tv_nsec + ((unsigned long)curr_time.tv_sec) * BILLION;
}

#ifdef USERDTSC

// this coefficient updated after prifiling RDTSC in profile_RDTSC
double tick_to_nsec_coef = 1.0;
inline unsigned long get_fast_time() {
    uint32_t low, high;
    asm volatile ("rdtsc" : "=a" (low), "=d" (high));
    return (unsigned long)(tick_to_nsec_coef * (((unsigned long)high << 32) | low));
}

bool profile_RDTSC() {
    std::cout << "Profiling timer....\n";

    auto ctime1 = gettime_helper();
    auto rdtsc1 = get_fast_time();

    const int RDTSC_PRIFILE_SECONDS = 3;
    timespec stime{RDTSC_PRIFILE_SECONDS, 0};
    if (0 > nanosleep(&stime, nullptr)) {
        if (errno != EINTR) {
            perror("nanosleep failed");
            return false;
        }
    }

    auto ctime2 = gettime_helper();
    auto rdtsc2 = get_fast_time();

    tick_to_nsec_coef = (double)(ctime2 - ctime1) / (double)(rdtsc2 - rdtsc1);

    std::cout << "Tick to ns coefficient = " << tick_to_nsec_coef << "\n";
    return true;
}

#endif

const int tab64[64] = {
    63,  0, 58,  1, 59, 47, 53,  2,
    60, 39, 48, 27, 54, 33, 42,  3,
    61, 51, 37, 40, 49, 18, 28, 20,
    55, 30, 34, 11, 43, 14, 22,  4,
    62, 57, 46, 52, 38, 26, 32, 41,
    50, 36, 17, 19, 29, 10, 13, 21,
    56, 45, 25, 31, 35, 16,  9, 12,
    44, 24, 15,  8, 23,  7,  6,  5};

int log2_64(uint64_t value) {
    value |= value >> 1;
    value |= value >> 2;
    value |= value >> 4;
    value |= value >> 8;
    value |= value >> 16;
    value |= value >> 32;
    return tab64[((uint64_t)((value - (value >> 1))*0x07EDD5E59A4E28C2)) >> 58];
}

bool connect_all(int sock_count,
                 std::vector<int> & sockets,
                 const char * ip,
                 const int port)
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

        sockets.push_back(sockfd);

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

    return true;
}

bool epoll_wait_ex(int epollfd,
                   EventsList & ready,
                   long int timeout_ns)
{
    bool already_polled = false;
    bool sleep_next_cycle = false;

    ready.num_ready = 0;

    if (timeout_ns < 0) {
        std::cerr << "timeout_ns for epoll_wait_ex should be >0\n";
        return false;
    }

    auto curr_time = get_fast_time();
    auto return_time = curr_time + timeout_ns;

    for(;;) {
        auto time_left = (long int)return_time - (long int)curr_time;

        if (time_left <= 0 and already_polled)
            return true;

        if (time_left < 0)
            time_left = 0;

        auto poll_timeout = time_left / 1000000;
        sleep_next_cycle = (0 == poll_timeout and 0 != time_left);

        ready.num_ready = epoll_wait(epollfd,
                                     &(ready.events[0]),
                                     ready.events.size(),
                                     poll_timeout);
        already_polled = true;
        curr_time = get_fast_time();

        if (0 == ready.num_ready) {
            if (curr_time >= return_time)
                return true;

            if (sleep_next_cycle) {
                // sleep 1us
                timespec stime{0, time_left};
                if (0 > nanosleep(&stime, nullptr)) {
                    if (errno != EINTR) {
                        perror("nanosleep failed");
                        return false;
                    }
                }
                curr_time = get_fast_time();
            }
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

        ready.recv_time = curr_time;
        return true;
    }
}


class DecOnExit {
public:
    std::atomic_int * counter;
    DecOnExit(std::atomic_int * _counter):counter(_counter){}
    ~DecOnExit() {--(*counter);}
};

bool ping(int fd, char * buff, int buff_sz) {
    int bc = recv(fd, buff, buff_sz, 0);
    if (ECONNRESET == errno) {
        return false;
    } else if (0 > bc) {
        std::perror("recv(fd, &buffer[0], buff_sz, 0)");
        return false;
    } else if (0 == bc) {
        perror("recv 0 bytes");
        return false;
    } else if (buff_sz != bc) {
        std::perror("partial message");
        return false;
    }

    if (buff_sz != write(fd, buff, buff_sz)) {
        std::perror("write(fd, &buffer[0], buff_sz)");
        return false;
    }
    return true;
}


struct FdTimout {
    int fd;
    unsigned long int ready_time;

    FdTimout(int _fd, unsigned long int _ready_time):fd(_fd), ready_time(_ready_time){}
    bool operator<(const FdTimout & fd)const {
        return ready_time > fd.ready_time;
    }
};


void worker_thread_fast(int epollfd,
                        int message_len,
                        unsigned long int timeout_ns,
                        std::atomic_bool * done,
                        std::atomic_int * active_count,
                        TestResult * result)
{
    if (0 != timeout_ns) {
        std::cerr << "worker_thread_fast doesn't support timeout\n";
        return;   
    }

    DecOnExit exitor(active_count);
    result->mcount = 0;

    EventsList elist;
    elist.events.resize(1024);

    std::vector<char> buffer;
    buffer.resize(message_len);

    for(;;) {
        elist.num_ready = epoll_wait(epollfd,
                                     &(elist.events[0]),
                                     elist.events.size(),
                                     100);
        if (elist.num_ready < 0 ) {
            perror("epoll_wait");
            return;
        }

        if (done->load())
            return;

        for (const auto & event: elist) {
            if (not ping(event.data.fd, &buffer[0], message_len))
                return;
        }
        result->mcount += elist.num_ready;
    }
}


void worker_thread(int epollfd,
                   int message_len,
                   unsigned long int timeout_ns,
                   std::atomic_bool * done,
                   std::atomic_int * active_count,
                   TestResult * result)
{
    DecOnExit exitor(active_count);
    std::unordered_map<int, unsigned long> last_time_for_socket;
    // std::map<int, unsigned long> last_time_for_socket;
    result->mcount = 0;

    EventsList elist;
    elist.events.resize(1024);

    std::vector<char> buffer;
    buffer.resize(message_len);

    std::vector<int> ready_fds;
    ready_fds.reserve(1024);

    std::priority_queue<FdTimout> wait_queue;

    for(;;) {
        ready_fds.clear();
        unsigned long int curr_time;

        // if there a ready sockets, waiting for timeout
        // need to not sleep too long in epoll
        if (wait_queue.size() > 0) {
            curr_time = get_fast_time();
            long int poll_timeout = wait_queue.top().ready_time - curr_time;

            if (poll_timeout < 0)
                poll_timeout = 0;

            if (not epoll_wait_ex(epollfd, elist, poll_timeout))
                return;

            // fill ready_fds with sockets
            // with expired timeouts
            curr_time = get_fast_time();
            while(wait_queue.size() > 0) {
                auto & item = wait_queue.top();
                // if timeout expires - remove from timeout_wait_queue
                // and put to ready_fds
                if (item.ready_time <= curr_time) {
                    ready_fds.push_back(item.fd);
                    wait_queue.pop();
                } else
                    break;
            }
        } else {
            if (not epoll_wait_ex(epollfd, elist, 100 * 1000 * 1000))
                return;
            curr_time = get_fast_time();
        }


        if (done->load())
            return;

        // go throught all polled fds, calculated latency
        // and move some to wait_queue
        for (const auto & event: elist) {
            auto fd = event.data.fd;

            auto item = last_time_for_socket.emplace(fd, 0);

            // previous write time for curr socket
            auto ltime = item.first->second;

            // if have previous write time for curr socket
            if (not item.second) {
                auto tout_l2 = log2_64(curr_time - ltime);

                if (tout_l2 >= (int)result->lat_ns_log2.size())
                    tout_l2 = result->lat_ns_log2.size() - 1;

                // increment array of log2-scale counters
                ++(result->lat_ns_log2[tout_l2]);
            }

            // if has timeout
            if (timeout_ns > 0) {

                // if socket isn't ready for new ping yet
                // put it into wait_queue
                if (ltime + timeout_ns > curr_time) {
                    wait_queue.emplace(fd, ltime + timeout_ns);
                    continue;
                }
            }

            ready_fds.push_back(fd);
        }

        for(auto fd: ready_fds) {
            if (done->load())
                return;

            if (not ping(fd, &buffer[0], message_len))
                return;

            last_time_for_socket[fd] = get_fast_time();
        }

        result->mcount += elist.num_ready;
    }
}

bool run_test(const TestParams & params, TestResult & res, int worker_threads)
{
    FDList sockets;

    if (not connect_all(params.num_conn, sockets.fds, params.ip, params.port))
        return false;

    FDList efd_list;

    epoll_event event;
    event.events = EPOLLIN | EPOLLET;

    if (worker_threads > params.num_conn)
        worker_threads = params.num_conn;

    int step = params.num_conn / worker_threads;

    for(int i = 0; i < worker_threads ; ++i) {
        int efd = epoll_create1(0);
        if (-1 == efd) {
            perror("epoll_create");
            return false;
        }
        efd_list.fds.push_back(efd);

        auto begin_iter = sockets.fds.begin() + step * i;
        auto end_iter = (
            i == worker_threads - 1 ? sockets.fds.end() : sockets.fds.begin() + step * (i + 1)
        );

        if (begin_iter == end_iter) {
            std::cerr << "An issue\n";
            return false;
        }

        for(; begin_iter != end_iter ; ++begin_iter) {
            event.data.fd = *begin_iter;
            if (-1 == epoll_ctl(efd, EPOLL_CTL_ADD, *begin_iter, &event)) {
                perror("epoll_ctl");
                return false;
            }
        }
    }

    std::vector<TestResult> tresults;
    tresults.resize(worker_threads);
    std::atomic_bool done{false};
    std::vector<std::thread> workers;
    std::atomic_int active_count{worker_threads};

    for(int i = 0; i < worker_threads ; ++i)
        workers.emplace_back(worker_thread,
                             efd_list.fds[i],
                             params.message_len,
                             params.timeout,
                             &done,
                             &active_count,
                             &tresults[i]);

    bool failed = false;
    char message[params.message_len];
    std::memset(message, 'X', sizeof(message));

    for(auto sock: sockets.fds) {
        if (params.message_len != write(sock, message, params.message_len)) {
            std::perror("write(sockfd, message, std::strlen(message))");
            failed = true;
            break;
        }
    }

    if (not failed) {
        int sleeps = params.runtime * 10;
        for(;sleeps > 0; --sleeps) {
            usleep(100 * 1000);
            if (active_count.load() == 0) {
                break;
            }
        }
    }

    done.store(true);
    for(auto & worker: workers)
        worker.join();

    res.mcount = 0;
    res.lat_ns_log2.fill(0);
    for(auto & ires: tresults) {
        res.mcount += ires.mcount;
        for(int pos = 0; pos < (int)res.lat_ns_log2.size(); ++pos)
            res.lat_ns_log2[pos] += ires.lat_ns_log2[pos];
    }


    // std::cout << std::hex << (unsigned int)guard1 << " " << (unsigned int)guard2 << std::dec << "\n";
    // std::cout << "res.mcount = " << res.mcount << "\n";

    return not failed;
}

void process_client(int sock) {
    FDCloser fdc{sock};
    char buff[MAX_CLIENT_MESSAGE + 1];
    int data_len = recv(sock, buff, sizeof(buff), 0);

    if (data_len < 0){
        perror("recv failed");
        return;
    }

    if (data_len == sizeof(buff)) {
        std::cerr << "Message to large\n";
        return;
    }
    buff[data_len] = 0;

    std::cout << "Get test spec '" << buff << "'\n";

    // MESSAGE FORMAT
    // CLIENT_IP - CLIENT_PORT - NUM_CONNECTIONS - RUNTIME - TIMEOUT - MESS_SIZE
    TestParams params;
    if (not load_from_str(buff, params))
        return;

    const int worker_thread = 3;
    TestResult res;
    if (not run_test(params, res, worker_thread))
        return;

    std::string responce = serialize_to_str(res);
    std::cout << "Test finished. Results : " << responce << "\n";
    if( write(sock, &responce[0], responce.size()) != (int)responce.size()) {
        perror("write failed");
        return;
    }
    return;
}

void *get_in_addr(struct sockaddr *sa)
{
    if (sa->sa_family == AF_INET)
        return &(((struct sockaddr_in*)sa)->sin_addr);
    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

int main_loop_thread(int port, bool single_shot=false) {
    sockaddr_in server, client;

    // this requires in order to fir write issue
    if (SIG_ERR  == signal(SIGPIPE, SIG_IGN)) {
        perror("signal(SIGPIPE, SIG_IGN) failed");
        return 1;
    }
     
    int control_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (-1 == control_sock){
        perror("Could not create socket");
        return 1;
    }

    int enable = 1;
    if (setsockopt(control_sock, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable)) < 0)
        perror("setsockopt(SO_REUSEADDR) failed");

    server.sin_family = AF_INET;
    server.sin_addr.s_addr = INADDR_ANY;
    server.sin_port = htons(port);

    if( 0 > bind(control_sock, (sockaddr *)&server , sizeof(server))) {
        perror("bind failed. Error");
        return 1;
    }
     
    listen(control_sock, 3);
    socklen_t sock_data_len = sizeof(client);
    
    for(;;){
        int client_sock = accept(control_sock, (sockaddr *)&client, &sock_data_len);
        if (client_sock < 0) {
            perror("accept failed");
            continue;
        }
        {
            char ipstr[INET6_ADDRSTRLEN];
            inet_ntop(client.sin_family, (void *)&client.sin_addr, ipstr, sizeof(ipstr));
            std::cout << "Client connected: " << ipstr << ":" << ntohs(client.sin_port) << "\n";
        }

        process_client(client_sock);

        if (single_shot)
            break;
    }
    return 0;
}

int main(int argc, const char **argv) {
    bool single_shot = false;
    if (argc > 1) {
        if (argv[1] == std::string("-s")) {
            single_shot = true;
        }
    }

#ifdef USERDTSC
    if (not profile_RDTSC())
        return 1;
#endif

    return main_loop_thread(DEFAULT_PORT, single_shot);
}
