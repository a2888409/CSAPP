#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <stdlib.h>
#include <cassert>
#include <sys/epoll.h>
#include "locker.h"
#include "threadpool.h"
#include "http_conn.h"
#include "time_heap.h"

#define TIMESLOT 5
#define MAX_FD 65536
#define MAX_EVENT_NUMBER 10000
static int pipefd[2];
static int epollfd = 0;
time_heap* heap = new time_heap(1024);

extern int addfd( int epollfd, int fd, bool one_shot );
//extern int removefd( int epollfd, int fd );
extern int setnonblocking( int fd);
extern void removefd(int epoll, int fd);
void sig_handler(int sig)
{
    int old_errno = errno;
    int msg = sig;
    send( pipefd[1], (char*)&msg, 1, 0);
    errno = old_errno;
}

void addsig(int sig)
{
    struct sigaction sa;
    memset( &sa, '\0', sizeof(sa));
    sa.sa_flags |= SA_RESTART;
    
    if(sig == SIGPIPE)
        sa.sa_handler = SIG_IGN;
    else
        sa.sa_handler = sig_handler;

    sigfillset(&sa.sa_mask);
    assert(sigaction(sig, &sa, NULL) != -1);
}

void time_handler()
{
    heap->tick();
    alarm( heap->top()->expire);
}

void cb_func(http_conn* user_data)
{
   removefd(epollfd, user_data->m_sockfd);
   printf("Close fd %d\n", user_data->m_sockfd);
}

void show_error( int connfd, const char* info )
{
    printf( "%s", info );
    send( connfd, info, strlen( info ), 0 );
    close( connfd );
}


int main( int argc, char* argv[] )
{
    if( argc <= 2 )
    {
        printf( "usage: %s ip_address port_number\n", basename( argv[0] ) );
        return 1;
    }
    const char* ip = argv[1];
    int port = atoi( argv[2] );

    int epollfd = epoll_create( 5 );
    assert( epollfd != -1 );

    int temp = socketpair( PF_UNIX, SOCK_STREAM, 0, pipefd);
    assert( temp != -1);
    setnonblocking( pipefd[1]);
    addfd( epollfd, pipefd[0],false);

    addsig( SIGPIPE);
    addsig( SIGHUP);
    addsig( SIGCHLD);
    addsig( SIGINT);
    bool stop_server = false;

    threadpool< http_conn >* pool = NULL;
    try
    {
        pool = new threadpool< http_conn >;
    }
    catch( ... )
    {
        return 1;
    }

    http_conn* users = new http_conn[ MAX_FD ];
    assert( users );
    int user_count = 0;

    int listenfd = socket( PF_INET, SOCK_STREAM, 0 );
    assert( listenfd >= 0 );
    struct linger tmp = { 1, 0 };
    setsockopt( listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof( tmp ) );

    int ret = 0;
    struct sockaddr_in address;
    bzero( &address, sizeof( address ) );
    address.sin_family = AF_INET;
    inet_pton( AF_INET, ip, &address.sin_addr );
    address.sin_port = htons( port );

    ret = bind( listenfd, ( struct sockaddr* )&address, sizeof( address ) );
    assert( ret >= 0 );

    ret = listen( listenfd, 5 );
    assert( ret >= 0 );

    epoll_event events[ MAX_EVENT_NUMBER ];
    addfd( epollfd, listenfd, false );
    http_conn::m_epollfd = epollfd;

    while( !stop_server )
    {
        int number = epoll_wait( epollfd, events, MAX_EVENT_NUMBER, -1 );
        if ( ( number < 0 ) && ( errno != EINTR ) )
        {
            printf( "epoll failure\n" );
            break;
        }

        for ( int i = 0; i < number; i++ )
        {
            int sockfd = events[i].data.fd;
            heap_timer* timer = users[sockfd].timer;
            
            if( sockfd == listenfd )
            {
                struct sockaddr_in client_address;
                socklen_t client_addrlength = sizeof( client_address );
                int connfd = accept( listenfd, ( struct sockaddr* )&client_address, &client_addrlength );
                if ( connfd < 0 )
                {
                    printf( "errno is: %d\n", errno );
                    continue;
                }
                if( http_conn::m_user_count >= MAX_FD )
                {
                    show_error( connfd, "Internal server busy" );
                    continue;
                }
                
                //创建一个定时器。
                heap_timer* timer = new heap_timer(5);
                timer->user_data = &users[connfd];
                timer->cb_func = cb_func;
                users[connfd].timer = timer;
                heap->add_timer(timer);

                users[connfd].init( connfd, client_address );
            }
            else if( (sockfd == pipefd[0]) && (events[i].events & EPOLLIN) )
            {
                int sig;
                char signals[1024];
                temp = recv( pipefd[0], signals, sizeof(signals), 0);
                    
                    for( int i=0; i<temp; i++)
                    {
                        switch( signals[i])
                        {
                            case SIGCHLD:
                            case SIGALRM:
                            {
                                time_handler();
                            }
                            case SIGHUP:
                                continue;
                            case SIGTERM:
                            case SIGINT:
                            {
                                printf("Recved Signal:SIGINT\n");
                                stop_server = true;
                            }
                        }
                    }
            }            
            else if( events[i].events & ( EPOLLRDHUP | EPOLLHUP | EPOLLERR ) )
            {
                cb_func(&users[sockfd]);
                users[sockfd].close_conn();
            }
            else if( events[i].events & EPOLLIN )
            {
                if( users[sockfd].read() )
                {
                    //有数据可读了
                    if( timer ) 
                    {
                        time_t cur = time(NULL);
                        timer->expire = cur + 3 * TIMESLOT;
                        printf("Adjust timer...\n");
                        heap->adjust_heap();
                        
                    }
                    pool->append( users + sockfd );
                }
                else
                {
                    cb_func(&users[sockfd]);
                    users[sockfd].close_conn();
                }
            }
            else if( events[i].events & EPOLLOUT )
            {
                if( !users[sockfd].write() )
                {
                    users[sockfd].close_conn();
                }
            }
            else
            {}
        }
    }

    close( epollfd );
    close( listenfd );
    delete [] users;
    delete pool;
    return 0;
}
