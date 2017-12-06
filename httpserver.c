#include <stdio.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <ctype.h>
#include <strings.h>
#include <string.h>
#include <sys/stat.h>
#include <pthread.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <assert.h>
#include "threadpool/threadpool.h"
#include "httpcore/httpcore.h"

int startup( u_short* );
void error_die( const char* );

#define THREAD 64
#define QUEUE  2000

int main( int argc, char const *argv[] ) {
	// 网络编程部分
	int server_sock = -1;
	u_short port = 0;
	int client_sock = -1;
	struct sockaddr_in client_name;
	int client_name_len = sizeof( client_name );
	pthread_t newthread;

	server_sock = startup( &port ); // 执行startup函数之后，会随机分配一个新的值给port。返回值的是已经执行了bind()的socket文件描述符
	printf( "httpd running on port %d\n", port );

	threadpool_t* pool = NULL; // threadpool_t是线程池的结构
	assert( ( pool = threadpool_create( THREAD, QUEUE, 0 ) ) != NULL ); // 创建一个线程池
	fprintf( stderr, "Pool started with %d threads and "
			"queue size of %d\n", THREAD, QUEUE );
    
    // I/O多路复用
    struct epoll_event ev, events[20];
	int epfd;
	int nfds = 0; // 用来接收epoll_wait的返回值，表示非阻塞的文件描述符的数量

    epfd = epoll_create(256);
    setnonblocking(server_sock);
    ev.data.fd = server_sock;
    ev.events = EPOLLIN|EPOLLET; // 当绑定的那个socket文件描述符可读的时候，就触发事件
    epoll_ctl(epfd, EPOLL_CTL_ADD, server_sock, &ev); // 把绑定的按个socket文件描述符添加到内核的红黑树里面


	while ( 1 ) {
		nfds = epoll_wait(epfd, events, 20, 500);
        
        int i;
        for (i = 0; i < nfds; ++i) {
        	if(events[i].data.fd == server_sock) { // 说明有新的客户端连接到来了
                client_sock = accept( server_sock, ( struct sockaddr * )&client_name, &client_name_len );
                if ( client_sock == -1 ) {
					printf("error_die(accept)");
					error_die( "accept" );
				}

				setnonblocking(client_sock);
                // 往线程池中的任务队列里面添加任务
            	if ( threadpool_add( pool, &accept_request, (void*)&events[i].data.fd, 0 ) != 0 ) { // 添加一个任务到线程池结构中的任务队列里面
					printf( "Job add error." );
				}
        	}
        }
	}

	assert( threadpool_destroy( pool, 0 ) == 0 );
	close( server_sock );
	printf("server_sock close");
	return( 0 );
}

/**********************************************************************/
/* This function starts the process of listening for web connections
* on a specified port.  If the port is 0, then dynamically allocate a
* port and modify the original port variable to reflect the actual
* port.
* Parameters: pointer to variable containing the port to connect on
* Returns: the socket */
/**********************************************************************/
int startup( u_short *port )
{
	int httpd = 0;
	struct sockaddr_in name; // 表示服务器的结构体

	httpd = socket( PF_INET, SOCK_STREAM, 0 );
	if ( httpd == -1 )
		error_die( "socket" );
	memset( &name, 0, sizeof( name ) ); // 注意
	name.sin_family = AF_INET;
	name.sin_port = htons( *port );
	name.sin_addr.s_addr = htonl( INADDR_ANY ); // 即0.0.0.0
	if ( bind( httpd, ( struct sockaddr * )&name, sizeof( name ) ) < 0 )
		error_die( "bind" );
	if ( *port == 0 )  { /* if dynamically allocating a port */
		int namelen = sizeof( name );
		/*
        getsockname()函数用于获取一个套接字的名字。
        它用于一个已捆绑或已连接套接字s，本地地址将被返回。
        本调用特别适用于如下情况：未调用bind()就调用了connect()，
        这时唯有getsockname()调用可以获知系统内定的本地地址。
        在返回时，namelen参数包含了名字的实际字节数。
		*/
		if ( getsockname( httpd, ( struct sockaddr * )&name, &namelen ) == -1 )
			error_die( "getsockname" );
		*port = ntohs( name.sin_port );
	}
	if ( listen( httpd, 256 ) < 0 )
		error_die( "listen" );
	return( httpd );
}

/**********************************************************************/
/* Print out an error message with perror() (for system errors; based
* on value of errno, which indicates system call errors) and exit the
* program indicating an error. */
/**********************************************************************/
void error_die( const char *sc ) {
	printf( sc );
	perror( sc );
	exit( 1 );
}

/*
作用：设置文件描述符的状态为非阻塞的
参数1：需要修改状态的文件描述符
*/
void setnonblocking(int sock) {
    int opts;
    opts=fcntl(sock,F_GETFL);
    if(opts<0) {
    	error_die("fcntl(sock,GETFL)");
    }
    opts = opts|O_NONBLOCK;
    if(fcntl(sock,F_SETFL,opts)<0) {
    	error_die("fcntl(sock,SETFL,opts)");
    }
}
