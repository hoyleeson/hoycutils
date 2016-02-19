#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <pthread.h>
#include <arpa/inet.h>

#include <common/log.h>
#include <common/netsock.h>

#define ERR_SOCK        (-1)
#define CONNECT_TIMEOUT	    (5)
#define MAXPENDING			(1)

/*tcp data infomation structure.*/
struct sock_stream {
    int sock_fd;
    /* listen address */
    struct sockaddr_in sock_addr; 

    int dest_fd;/*destination socket fd, if server, this is accept() return fd, otherwise equal the sock_fd.*/
    struct sockaddr_in dest_addr;
};



static int stream_connect(struct netsock* nsock);
static int stream_listen(struct netsock* nsock);
static void* stream_listen_th(void* args);

#ifdef RECV_USE_CALLBACK
static void* process_connection(void* arg);
#endif


/**
 * @brief   stream_init
 * 
 * initialize the tcp trans module. 
 * @author Li_Xinhai
 * @date 2012-06-26
 * @param[in] arg1:input infomation structure.
 * @return int return success or failed
 * @retval returns zero on success
 * @retval return a non-zero error code if failed
 */
static int stream_init(struct netsock* nsock)
{
    int optval = 0;

    struct sock_stream* stream;

    stream = (struct sock_stream*)malloc(sizeof(struct sock_stream));	
    if(!stream)
        loge("alloc tcp info struct fail.\n");

    stream->sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (stream->sock_fd == -1)
        return -1;

    bzero(&stream->sock_addr, sizeof(struct sockaddr_in));
    stream->sock_addr.sin_family = AF_INET;

    if((optval = fcntl(stream->sock_fd, F_GETFL, 0)) < 0)
        return -1;

    if(fcntl(stream->sock_fd, F_SETFL, optval & (~O_NONBLOCK))< 0)
        return -1;

    stream->dest_fd = ERR_SOCK;

    nsock->private_data = stream;

    if(nsock->args.is_server)	//if this library used by the server.
    {
        stream->sock_addr.sin_addr.s_addr = htonl(INADDR_ANY);
        stream->sock_addr.sin_port = htons(nsock->args.listen_port);	

        stream_listen(nsock);	//listen port
    }
    else
    {
        stream->sock_addr.sin_addr.s_addr = nsock->args.dest_ip;
        stream->sock_addr.sin_port = htons(nsock->args.dest_port);	

        stream_connect(nsock);	//connection the server.
    }

    logi("tcp init success.\n");
    return 0;
}


/**
 * @brief   stream_listen
 * 
 * Create stream_listen thread. 
 * @author Li_Xinhai
 * @date 2012-06-26
 * @param[in] arg1:input infomation structure.
 * @return int return success or failed
 * @retval returns zero on success
 * @retval return a non-zero error code if failed
 */
static int stream_listen(struct netsock* nsock)
{
    pthread_t thread;

    return pthread_create(&thread, NULL, stream_listen_th, nsock);
}




/**
 * @brief   stream_connect
 * 
 * client call this function connect the server 
 * @author Li_Xinhai
 * @date 2012-06-26
 * @param[in] arg1:input infomation structure.
 * @return int return success or failed
 * @retval returns zero on success
 * @retval return a non-zero error code if failed
 */
static int stream_connect(struct netsock* nsock)
{
    int ret;
    int flags;
    int result;
    struct timeval tm;
    fd_set c_fds;
    struct sock_stream* stream = nsock->private_data;

    ret = -1;	

    if((flags = fcntl(stream->sock_fd, F_GETFL, 0)) < 0)
        return -1;

    if(fcntl(stream->sock_fd, F_SETFL, flags | O_NONBLOCK)< 0)
        return -1;

    result = connect(stream->sock_fd, (struct sockaddr*)(&stream->sock_addr), sizeof(stream->sock_addr));

    if(result == 0)
    {
        logi("connect server success.\n");
        ret = 0;
    }
    else
    {
        if(errno == EINPROGRESS) 
        {
            tm.tv_sec  = CONNECT_TIMEOUT;
            tm.tv_usec = 0;
            FD_ZERO(&c_fds);
            FD_SET(stream->sock_fd, &c_fds);

            int n = select(stream->sock_fd+1, 0, &c_fds, 0, &tm);

            if(n == -1 && errno!= EINTR)
            {
                logw("server select error\n");			
            }
            else if(n == 0)
            {
                logw("connect server timeout\n");
            }
            else if(n > 0) 
            {               
                int optval;
                int optlen = 4;
                if(getsockopt(stream->sock_fd, SOL_SOCKET, SO_ERROR, (void*)&optval, (socklen_t*)&optlen) < 0) 
                    loge("tcp getsockopt fail!\n");

                if(optval == 0)
                {
                    ret=0;
                    logi("server success select\n");
                } 
                else
                {
                    logw("failed select %d:%s in the server\n", optval, strerror(optval));
                }
            } 
        }
        else 
        {
            close(stream->sock_fd);
            logw("connect server fail\n");
        }
    }

    if(fcntl(stream->sock_fd, F_SETFL, flags)< 0)
        return -1;

    stream->dest_fd = stream->sock_fd;
    stream->dest_addr = stream->sock_addr;
    return ret;
}


/**
 * @brief   stream_recv
 * 
 * tcp receive data function.
 * @author Li_Xinhai
 * @date 2012-06-26
 * @param[in] arg1:input infomation structure.
 * @return int return success or failed
 * @retval returns zero on success
 * @retval return a non-zero error code if failed
 */
static int stream_recv(struct netsock* nsock, void* data, int len)
{
    int ret;
    struct sock_stream* stream = nsock->private_data;

    while(stream->dest_fd == ERR_SOCK)
        sleep(1);

    ret = recv(stream->dest_fd, data, len, 0);

    /* If the other party closed socket, will also own a socket closed */
    if(ret <= 0) {
        close(stream->dest_fd);
        stream->dest_fd = ERR_SOCK;
    }

    return ret;
}


/**
 * @brief   stream_recv_timeout
 * 
 * tcp receive data function.
 * @author Li_Xinhai
 * @date 2012-06-26
 * @param[in] arg1:input infomation structure.
 * @return int return success or failed
 * @retval returns zero on success
 * @retval return a non-zero error code if failed
 */
static int stream_recv_timeout(struct netsock* nsock, void* data, int len, unsigned long ms)
{
    int ret, nready;
    fd_set rset;
    struct timeval timeout;

    struct sock_stream* stream = nsock->private_data;
    ret = -1;

    while(stream->dest_fd == ERR_SOCK)
        sleep(1);

    FD_ZERO(&rset);
    FD_SET(stream->dest_fd, &rset);

    timeout.tv_sec = ms/1000;
    timeout.tv_usec = ms%1000;

    nready = select(stream->dest_fd+1, &rset, NULL, NULL, &timeout);
    if(nready < 0)//select error.
    {
        return -1;
    }
    else if(nready == 0)//select timeout.
    {
        return 0;
    }
    else
    {
        ret = recv(stream->dest_fd, data, len, 0);

        if(ret <= 0)	//If the other party closed socket, will also own a socket closed 
        {
            close(stream->dest_fd);
            stream->dest_fd = ERR_SOCK;
        }
    }
    return ret;
}


/**
 * @brief   stream_send
 * 
 * stream_send
 * @author Li_Xinhai
 * @date 2012-06-26
 * @param[in] arg1: send data.
 * @param[in] arg1: send lenght.
 * @return int return success or failed
 * @retval returns zero on success
 * @retval return a non-zero error code if failed
 */
static int stream_send(struct netsock* nsock, void* data, int len)
{
    int ret;
    struct sock_stream* stream = nsock->private_data;

    ret = send(stream->dest_fd, data, len, 0);

    return (ret<0) ? -1:0;
}


/**
 * @brief   stream_serv_send
 * 
 * when the library to be call by the server, 
 * use this function send data. reserved method.
 * @author Li_Xinhai
 * @date 2012-06-26
 * @param[in] arg1: send data.
 * @param[in] arg1: send lenght.
 * @return int return success or failed
 * @retval returns zero on success
 * @retval return a non-zero error code if failed
 */
static int stream_serv_send(struct netsock *nsock, void *session, void *buf, int len)
{
    int ret;
    struct connection* conn = (struct connection*)session;

    ret = send(conn->sock_fd, buf, len, 0);

    return (ret<0) ? -1:0;
}


/**
 * @brief   stream_release
 * 
 * call this function destory the resource.
 * @author Li_Xinhai
 * @date 2012-06-26
 * @param[in] arg1:input infomation structure.
 * @return int return success or failed
 * @retval returns zero on success
 * @retval return a non-zero error code if failed
 */
static void stream_release(struct netsock* nsock)
{
    struct sock_stream* stream = nsock->private_data;

    close(stream->sock_fd);
    if(stream->dest_fd!=ERR_SOCK)
        close(stream->dest_fd);

    free(stream);
}


struct netsock_operations stream_ops = {
    .init       =  stream_init,
    .release    =  stream_release,
    .send       =  stream_send,
    .serv_send  =  stream_serv_send,
    .recv       =  stream_recv,
    .recv_timeout = stream_recv_timeout,
};




#ifndef RECV_USE_CALLBACK

/**
 * @brief   stream_listen_th
 * 
 * stream_listen_th.
 * @author Li_Xinhai
 * @date 2012-06-26
 * @param[in] arg1:input infomation structure.
 * @return int return success or failed
 * @retval returns zero on success
 * @retval return a non-zero error code if failed
 */
static void* stream_listen_th(void* args)
{
    int ret;
    int flag;
    int cli_sock;
    struct sockaddr_in cli_addr;
    socklen_t cli_len;
    struct sock_stream* stream;
    struct netsock* nsock;

    nsock = (struct netsock*)args;

    stream = (struct sock_stream*)nsock->private_data;

    stream->sock_addr.sin_addr.s_addr =htonl(INADDR_ANY); /* Any incoming interface */

    ret = bind(stream->sock_fd, (struct sockaddr* )&stream->sock_addr, sizeof cli_addr);
    if(ret < 0)
        return 0;

    ret = listen(stream->sock_fd, MAXPENDING);
    if(ret < 0)
        return 0;

    for( ; ; )
    {

        cli_len = sizeof cli_addr;

        cli_sock = accept(stream->sock_fd, (struct sockaddr*) &cli_addr, &cli_len);

        logd("accept new client. fd = %d\n", cli_sock);

        stream->dest_fd = cli_sock;
        stream->dest_addr = cli_addr;

        if((flag = fcntl(stream->dest_fd, F_GETFL, 0)) < 0)
            return 0;

        if(fcntl(stream->dest_fd, F_SETFL, flag & (~O_NONBLOCK))< 0)
            return 0;
    }

    return 0;
}

#else



struct process_param
{
    int sock_fd;
    struct sockaddr_in sock_addr;
    struct netsock* owner;
};


/**
 * @brief   stream_listen_th
 * 
 * stream_listen_th.
 * @author Li_Xinhai
 * @date 2012-06-26
 * @param[in] arg1:input infomation structure.
 * @return int return success or failed
 * @retval returns zero on success
 * @retval return a non-zero error code if failed
 */
static void* stream_listen_th(void* args)
{
    int ret;
    pthread_t thread_conn;
    int cli_sock;
    struct sockaddr_in cli_addr;
    socklen_t cli_len;
    struct process_param* p;
    struct netsock* nsock;
    struct sock_stream* stream;

    nsock = (struct netsock*)args;
    stream = (struct sock_stream*)nsock->private_data;


    stream->sock_addr.sin_addr.s_addr =htonl(INADDR_ANY); /* Any incoming interface */

    ret = bind(stream->sock_fd, (struct sockaddr* )&stream->sock_addr, sizeof cli_addr);
    if(ret < 0)
        return 0;

    ret = listen(stream->sock_fd, MAXPENDING);
    if(ret < 0)
        return 0;

    for( ; ; )
    {
        cli_len = sizeof cli_addr;

        cli_sock = accept(stream->sock_fd, (struct sockaddr*) &cli_addr, &cli_len);

        logd("accept new client. fd = %d\n", cli_sock);

        p = (struct process_param*)malloc(sizeof(struct process_param));
        p->sock_fd = cli_sock;
        p->sock_addr = cli_addr;
        p->owner = nsock;
        pthread_create(&thread_conn, NULL, process_connection, p);

    }

    return 0;
}

#endif


#ifdef RECV_USE_CALLBACK
/*if define RECV_USE_CALLBACK */

/**
 * @brief   process_connection
 * 
 * process_connection.
 * @author Li_Xinhai
 * @date 2012-06-26
 * @param[in] arg1:input infomation structure.
 * @return int return success or failed
 * @retval returns zero on success
 * @retval return a non-zero error code if failed
 */
static void* process_connection(void* arg)
{
    int running = 1;
    int ret;
    int nready;
    struct process_param* param;
    fd_set fds;

    char buf[2048];

    struct timeval timeout;
    struct net_packet pack;
    struct netsock *nsock;
    struct sock_stream *stream;

    param = (struct process_param*)arg;
    nsock = param->owner;
    stream = nsock->private_data;

    pack.data = malloc(nsock->args.buf_size);
    logd("process connection running. recv socket is %d.\n", param->sock_fd);

    while(running)
    {
        FD_ZERO(&fds);
        FD_SET(param->sock_fd, &fds); 
        timeout.tv_sec = 5;
        timeout.tv_usec = 0;

        nready = select(param->sock_fd+1, &fds, NULL, NULL, &timeout);

        if(nready<=0)  
        {
            usleep(1000);
            continue;
        }
        else
        {
            if ((pack.datalen = recv(param->sock_fd, pack.data, nsock->args.buf_size, 0)) <= 0)
            {
                close(param->sock_fd);
                FD_CLR(param->sock_fd, &fds);
                param->sock_fd = -1;
                break;
            }
            pack.conn.sock_fd = param->sock_fd;
            pack.conn.sock_addr = param->sock_addr;

            if(nsock->args.recv_cb)
                nsock->args.recv_cb(&pack);
        }
        usleep(10);
    }

    logd("tcp socket %d close.\n", param->sock_fd);
    close(param->sock_fd);

    return 0;
}
#endif


