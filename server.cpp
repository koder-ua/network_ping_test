#include <map>
#include <queue>
#include <mutex>
#include <atomic>
#include <chrono>
#include <vector>
#include <thread>
#include <random>
#include <cstring>
#include <sstream>
#include <iomanip>
#include <iostream>
#include <algorithm>
#include <unordered_map>

#ifndef LOG2_LAT
#include <cmath>
#endif

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


struct TestParams {
    int port, num_conn, runtime, message_len;
    unsigned long int min_timeout, max_timeout;
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
    ~FDCloser() {
        close(fd);
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

#ifdef LOG2_LAT
const int LAT_ARR_SIZE = 30;
#else
const int LAT_ARR_SIZE = 300;
#endif

struct TestResult{
    unsigned long mcount;
    unsigned long avg_lat_ns;
    std::array<unsigned long, 19> percentiles;
    std::unordered_map<unsigned long, unsigned long> lat_map;
    std::unordered_map<int, unsigned long> mess_count_for_sock;
};

class DecOnExit {
public:
    std::atomic_int * counter;
    DecOnExit(std::atomic_int * _counter):counter(_counter){}
    ~DecOnExit() {--(*counter);}
};

struct FdTimout {
    int fd;
    unsigned long int ready_time;

    FdTimout(int _fd, unsigned long int _ready_time):fd(_fd), ready_time(_ready_time){}
    bool operator<(const FdTimout & fd)const {
        return ready_time > fd.ready_time;
    }
};

struct Sync {
   std::atomic_bool done;
   std::mutex run_lola_run;
   std::atomic_int active_count;
};

std::string serialize_to_str(const TestResult & res) {
    std::stringstream serialized;
    serialized << res.mcount;

    #ifdef LOG2_LAT
    serialized << " 2";
    #else
    serialized << " " << std::setprecision(12) << std::pow(2L, 0.1L);
    #endif

    serialized << " " << res.lat_map.size();
    for(const auto & val: res.lat_map)
        serialized << " " << val.first << " " << val.second;

    serialized << " " << res.percentiles.size();
    for(auto val: res.percentiles)
        serialized << " " << val;

    return serialized.str();
}

bool load_from_str(const char * data, TestParams & params) {
    if (std::strlen(data) > sizeof(params.ip)) {
        std::cerr << "Message too large\n";
        return false;
    }
    int num_scanned = std::sscanf(data, "%s %d %d %d %lu %lu %d",
                                  params.ip,
                                  &params.port,
                                  &params.num_conn,
                                  &params.runtime,
                                  &params.min_timeout,
                                  &params.max_timeout,
                                  &params.message_len);
    if (num_scanned != 7) {
        std::cerr << "Message from client is broken '" << data << "'\n";
        return false;
    }

    if (params.min_timeout > params.max_timeout) {
        std::cerr << "Message from client is broken. (min_timeout)" << params.min_timeout;
        std::cerr << " > (max_timeout) " << params.min_timeout << "\n";
        std::cerr << " data = '" << data << "'\n";
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

    // using namespace std::chrono;
    // auto curr_time = high_resolution_clock::now().time_since_epoch();
    // return (unsigned long) duration_cast<nanoseconds>(curr_time).count();
}

#ifdef USERDTSC
// this coefficient updated after prifiling RDTSC in profile_RDTSC from main
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

int log2_64(uint64_t value) {
    const int tab64[64] = {
        63,  0, 58,  1, 59, 47, 53,  2,
        60, 39, 48, 27, 54, 33, 42,  3,
        61, 51, 37, 40, 49, 18, 28, 20,
        55, 30, 34, 11, 43, 14, 22,  4,
        62, 57, 46, 52, 38, 26, 32, 41,
        50, 36, 17, 19, 29, 10, 13, 21,
        56, 45, 25, 31, 35, 16,  9, 12,
        44, 24, 15,  8, 23,  7,  6,  5};

    value |= value >> 1;
    value |= value >> 2;
    value |= value >> 4;
    value |= value >> 8;
    value |= value >> 16;
    value |= value >> 32;

    return tab64[((uint64_t)((value - (value >> 1))*0x07EDD5E59A4E28C2)) >> 58];
}


bool wait_sock_connected(int sockfd, int poll_timeout=1000) {
    epoll_event event;
    event.events = EPOLLIN | EPOLLET;

    std::vector<epoll_event> events;
    events.resize(1);

    int efd = epoll_create1(0);
    FDCloser efd_{efd};

    if (-1 == efd) {
        perror("epoll_create1");
        return false;
    }

    event.data.fd = sockfd;
    if (-1 == epoll_ctl(efd, EPOLL_CTL_ADD, sockfd, &event)) {
        perror("epoll_ctl(EPOLL_CTL_ADD)");
        return false;
    }

    int num_ready = epoll_wait(efd,
                               &(events[0]),
                               events.size(),
                               poll_timeout);
    if (num_ready < 0) {
        perror("epoll_wait(CONNECT)");
        return false;
    }

    if (num_ready == 0) {
        std::cerr << "Socket failed to connect NR\n";
        return false;
    }

    int error = 0;
    socklen_t len = sizeof(error);
    int retval = getsockopt(sockfd, SOL_SOCKET, SO_ERROR, &error, &len);
    if (0 > retval) {
        perror("getsockopt(...):");
        return false;
    }

    if (error != 0) {
        std::cerr << "Socket failed to connect ERROR\n";
        return false;
    }

    return true;
}


bool connect_all(int sock_count,
                 std::vector<int> & sockets,
                 const char * ip,
                 const int port,
                 const std::vector<sockaddr_in> & client_ip_addrs)
{
    const struct hostent * host = gethostbyname(ip);
    if (NULL == host) {
        std::string message("No such host: '");
        message.append(ip);
        message.append("'");
        std::perror(message.c_str());
        return false;
    }

    struct sockaddr_in serv_addr;
    bzero((char *)&serv_addr, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    bcopy((const char *)host->h_addr, (char *)&serv_addr.sin_addr.s_addr, host->h_length);
    serv_addr.sin_port = htons(port);
    sockets.clear();

    auto curr_it = client_ip_addrs.begin();
    auto end_it = client_ip_addrs.end();
    bool need_bind = (curr_it != end_it);

    for(int i = 0; i < sock_count ; ++i) {
        int sockfd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
        if (sockfd < 0) {
            std::perror("Socket creation:");
            return false;
        }

        sockets.push_back(sockfd); // external code would close all ports from sockets

        const int enable{1};
        if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable)) < 0)
            perror("setsockopt(SO_REUSEADDR) failed");

        if (need_bind) {
            if (curr_it == end_it)
                curr_it = client_ip_addrs.begin();
            if ( 0 > bind(sockfd, (struct sockaddr *)&*curr_it, sizeof(*curr_it))) {
                std::perror("Client bind:");
                return false;
            }
            ++curr_it;
        }

        if (0 > connect(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr))) {
            if (errno != EINPROGRESS) {
                std::perror("Connecting:");
                return false;
            }

            if (not wait_sock_connected(sockfd)) {
                return false;
            }
        }

        // int flags = fcntl(sockfd, F_GETFL, 0);
        // if (flags < 0) {
        //     std::perror("fcntl(sockfd, F_GETFL, 0)");
        //     return false;
        // }
        //
        // if (fcntl(sockfd, F_SETFL, flags | O_NONBLOCK) < 0) {
        //     std::perror("fcntl(sockfd, F_SETFL, flags | O_NONBLOCK)");
        //     return false;
        // }
    }

    return true;
}

#ifdef EPOLL_CALL_STATS
std::atomic<unsigned long int> socket_count_from_wait;
std::atomic<unsigned int> epoll_wait_calls;
#endif

bool epoll_wait_ex(int epollfd,
                   EventsList & ready,
                   long int timeout_ns)
{
    bool already_polled = false;

    ready.num_ready = 0;

    if (timeout_ns < 0) {
        std::cerr << "timeout_ns for epoll_wait_ex should be >0\n";
        return false;
    }

    auto curr_time = get_fast_time();
    auto return_time = curr_time + timeout_ns;

    for(;;) {
        auto time_left = (long int)return_time - (long int)curr_time;

        if (time_left <= 0) {
            if (already_polled)
                return true;
            else
                time_left = 0;
        }

        auto poll_timeout = time_left / 1000000;

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

bool ping(int fd, char * buff, int buff_sz) {
    int bc = recv(fd, buff, buff_sz, 0);
    if (0 > bc and ECONNRESET == errno) {
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

void worker_thread_fast(int epollfd,
                        int message_len,
                        int sock_count,
                        unsigned long int timeout_ns_min,
                        unsigned long int timeout_ns_max,
                        Sync * sync,
                        TestResult * result)
{
    if (0 != timeout_ns_min or 0 != timeout_ns_max) {
        std::cerr << "worker_thread_fast doesn't support timeouts\n";
        return;
    }

    DecOnExit exitor(&sync->active_count);
    result->mcount = 0;

    EventsList elist;
    elist.events.resize(sock_count);

    std::vector<char> buffer;
    buffer.resize(message_len);

    sync->active_count++;
    sync->run_lola_run.lock();
    sync->run_lola_run.unlock();

    for(;;) {
        elist.num_ready = epoll_wait(epollfd,
                                     &(elist.events[0]),
                                     elist.events.size(),
                                     100);
        if (elist.num_ready < 0 ) {
            perror("epoll_wait");
            return;
        }

        if (sync->done.load())
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
                   int sock_count,
                   unsigned long timeout_ns_min,
                   unsigned long timeout_ns_max,
                   Sync * sync,
                   TestResult * result)
{
    std::unordered_map<int, unsigned long> last_time_for_socket;
    result->mcount = 0;

    std::mt19937 rand_gen;
    std::uniform_int_distribution<unsigned long> rand_timeout(timeout_ns_min, timeout_ns_max);

    bool has_timeout = (0 != timeout_ns_min) or (0 != timeout_ns_max);
    EventsList elist;
    elist.events.resize(sock_count);

    std::vector<char> buffer;
    buffer.resize(message_len);

    std::vector<int> ready_fds;
    ready_fds.reserve(sock_count);

    std::priority_queue<FdTimout> wait_queue;

    sync->active_count++;
    DecOnExit exitor(&sync->active_count);
    sync->run_lola_run.lock();
    sync->run_lola_run.unlock();

    for(;;) {
        ready_fds.clear();
        unsigned long curr_time;

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


        if (sync->done.load())
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

                #ifdef LOG2_LAT
                int tout_l2 = (int)log2_64(curr_time - ltime);
                #else
                int tout_l2 = std::lround(std::log2((float)(curr_time - ltime)) * 10);
                #endif

                result->lat_map.emplace(tout_l2, 0).first->second++;
            }

            // if has timeout
            if (has_timeout) {
                unsigned long timeout_ns = 0;
                if (timeout_ns_max != timeout_ns_min) {
                    timeout_ns = rand_timeout(rand_gen);
                } else {
                    timeout_ns = timeout_ns_max;
                }

                // if socket isn't ready for new ping yet
                // put it into wait_queue
                if (ltime + timeout_ns > curr_time) {
                    wait_queue.emplace(fd, ltime + timeout_ns);
                    continue;
                }
            }

            ready_fds.push_back(fd);
            if (sync->done.load())
                return;
        }

        for(auto fd: ready_fds) {
            if (sync->done.load())
                return;

            if (not ping(fd, &buffer[0], message_len))
                return;

            last_time_for_socket[fd] = get_fast_time();
            result->mess_count_for_sock.emplace(fd, 0).first->second++;
        }

        result->mcount += elist.num_ready;
    }
}

bool run_test(const TestParams & params, TestResult & res, int worker_threads,
              const char ** first_ip, const char ** last_ip)
{
    FDList sockets;
    std::vector<sockaddr_in> client_ip_addrs;

    struct sockaddr_in localaddr;
    localaddr.sin_family = AF_INET;
    localaddr.sin_port = 0;

    for(; first_ip != last_ip; ++first_ip) {
        localaddr.sin_addr.s_addr = inet_addr(*first_ip);
        client_ip_addrs.push_back(localaddr);
    }

    if (not connect_all(params.num_conn, sockets.fds, params.ip, params.port, client_ip_addrs))
        return false;

    // 1s sleep, allow client to actually accept all connections
    // as some of already connected sockets may be in listen buffer, and not processed
    // by client yet
    usleep(1000 * 1000);

    FDList efd_list;

    epoll_event event;
    event.events = EPOLLIN | EPOLLET;

    worker_threads = std::min(params.num_conn, worker_threads);

    for(int i = 0; i < worker_threads ; ++i) {
        int efd = epoll_create1(0);
        if (-1 == efd) {
            perror("epoll_create");
            return false;
        }
        efd_list.fds.push_back(efd);
    }

    int idx = 0;
    for(auto fd: sockets.fds) {
        event.data.fd = fd;
        int efd = efd_list.fds[idx % worker_threads];
        if (-1 == epoll_ctl(efd, EPOLL_CTL_ADD, fd, &event)) {
            perror("epoll_ctl");
            return false;
        }
        ++idx;
    }

    std::vector<TestResult> tresults;
    tresults.resize(worker_threads);

    std::vector<std::thread> workers;
    Sync sync;

    sync.done = false;
    sync.active_count = 0;
    sync.run_lola_run.lock();

    int max_sock_count_per_worker = params.num_conn / worker_threads + 1;
    for(int i = 0; i < worker_threads ; ++i)
        workers.emplace_back(worker_thread,
                             efd_list.fds[i],
                             params.message_len,
                             max_sock_count_per_worker,
                             params.min_timeout,
                             params.max_timeout,
                             &sync,
                             &tresults[i]);

    bool failed = false;
    std::string message((size_t)params.message_len, 'X');

    for(auto sock: sockets.fds) {
        if (params.message_len != write(sock, message.c_str(), message.length())) {
            std::perror("write(sock, message, ...)");
            failed = true;
            break;
        }
    }

    if (not failed) {
        while (sync.active_count.load() != worker_threads)
                usleep(100 * 1000); // 100ms sleep

        sync.run_lola_run.unlock();

        // run threads for params.runtime seconds
        int sleeps = params.runtime * 10;
        for(;sleeps > 0; --sleeps) {
            usleep(100 * 1000); // 100ms sleep
            if (sync.active_count.load() == 0)
                break;
        }
    }

    sync.done.store(true);
    for(auto & worker: workers)
        worker.join();

    res.mcount = 0;

    for(const auto & ires: tresults) {
        res.mcount += ires.mcount;
        for(const auto & lat_ref: ires.lat_map)
            res.lat_map.emplace(lat_ref.first, 0).first->second += lat_ref.second;
    }

    std::vector<unsigned long> mps;
    mps.reserve(params.num_conn);

    for(const auto & ires: tresults) {
        for(const auto & item: ires.mess_count_for_sock)
            mps.push_back(item.second);
    }

    std::sort(begin(mps), end(mps));

    for(int i = 0 ; i < (int)res.percentiles.size() ; ++i) {
        int idx = params.num_conn * (i + 1) / (res.percentiles.size() + 1);
        res.percentiles[i] = mps[idx];
    }

    #ifdef LOG2_LAT
    double base = 2.0;
    #else
    double base = std::pow(2L, 0.1L);
    #endif

    long count = 0;
    double lat_ns_sum = 0;

    for(const auto & lat_ref: res.lat_map) {
        lat_ns_sum += lat_ref.second * std::pow(base, lat_ref.first);
        count += lat_ref.second;
    }

    res.avg_lat_ns = (long) (lat_ns_sum / count);
    return not failed;
}

void process_client(int sock, const char ** first_ip, const char ** last_ip, int max_wait_time_seconds=5) {
    FDCloser fdc{sock};
    char buff[MAX_CLIENT_MESSAGE + 1];
    int data_len = 0;

    usleep(100 * 1000); // 100ms sleep
    for(int i = 0 ; i <= max_wait_time_seconds * 10; ++i) {
        data_len = recv(sock, buff, sizeof(buff), MSG_DONTWAIT);
        if (data_len < 0 and not (errno == EAGAIN or errno == EWOULDBLOCK)) {
            perror("recv failed");
            return;
        }

        if (data_len > 0)
            break;
        usleep(100 * 1000); // 100ms sleep
    }

    if (data_len <= 0) {
        std::cerr << "Client communication timeout\n";
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
    if (not run_test(params, res, worker_thread, first_ip, last_ip))
        return;

    std::cout << "Test finished. Results : " << "\n";
    std::cout << "    mess_count = " << res.mcount << "\n";
    std::cout << "    average_mps = " << res.mcount / params.runtime << "\n";
    std::cout << "    average_lat = " << (int)(res.avg_lat_ns / 1000) << " us\n";
    std::cout << "    5% mess perc = " << res.percentiles[0] << "\n";
    std::cout << "    95% mess perc = " << res.percentiles[res.percentiles.size() - 1] << "\n";

    std::string responce = serialize_to_str(res);
    if( write(sock, &responce[0], responce.size()) != (int)responce.size()) {
        perror("write failed");
        return;
    }
    return;
}

void *get_in_addr(struct sockaddr *sa) {
    if (sa->sa_family == AF_INET)
        return &(((struct sockaddr_in*)sa)->sin_addr);
    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

int main_loop_thread(int port, bool single_shot, const char ** first_ip, const char ** last_ip) {
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

        #ifdef EPOLL_CALL_STATS
        socket_count_from_wait = 0;
        epoll_wait_calls = 0;
        #endif

        process_client(client_sock, first_ip, last_ip);

        #ifdef EPOLL_CALL_STATS
        if ( 0 != epoll_wait_calls.load()) {
            std::cout << "Average sockets from epoll_wait = ";
            std::cout << socket_count_from_wait / epoll_wait_calls << "\n";
        }
        #endif

        if (single_shot)
            break;
    }
    return 0;
}

int main(int argc, const char **argv) {
    bool single_shot = false;

    const char ** first_ip = argv + 1;
    const char ** last_ip = argv + argc;

    if (argc > 1) {
        if (argv[1] == std::string("-s")) {
            single_shot = true;
            ++first_ip;
        }
    }

#ifdef USERDTSC
    if (not profile_RDTSC())
        return 1;
#endif

    return main_loop_thread(DEFAULT_PORT, single_shot, first_ip, last_ip);
}
