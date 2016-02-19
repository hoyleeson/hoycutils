#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <pthread.h>
#include <arpa/inet.h>

#include <common/log.h>
#include <common/netsock.h>

struct sock_dgram {
	int recv_fd;
	/* listen address */
	struct sockaddr_in recv_addr; 
	int send_fd;
	/* send address */
	struct sockaddr_in send_addr;

	int recv_running;
};

#ifdef RECV_USE_CALLBACK
static void start_recv_th(struct netsock* nsock);
#endif


/**
* @brief   dgram_init
* 
* initialize the udp trans module. 
* @author Li_Xinhai
* @date 2012-06-26
* @param[in] arg1:input infomation structure.
* @return int return success or failed
* @retval returns zero on success
* @retval return a non-zero error code if failed
*/
static int dgram_init(struct netsock* nsock)
{
	int ret;
	int optval;
	struct sock_dgram* dgram;

	dgram = (struct sock_dgram*)malloc(sizeof(struct sock_dgram));
	if(!dgram)
		fatal("alloc sock dgram failed.\n");

	if((dgram->recv_fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
		loge("create recv socket fail. ret is %d\n", dgram->recv_fd);
        ret = -EINVAL;
        goto failed;
    }

	optval = 1;
	if (setsockopt(dgram->recv_fd, SOL_SOCKET, SO_REUSEADDR,
				 (const void *)&optval, sizeof(int)) < 0) {
		loge("recv_sock udp setsockopt failed.\n");
        ret = -EINVAL;
        goto failed;
    }
	
	memset(&dgram->recv_addr, 0, sizeof(struct sockaddr_in));  
	dgram->recv_addr.sin_family = AF_INET;       
	dgram->recv_addr.sin_addr.s_addr = htonl(INADDR_ANY); 
	dgram->recv_addr.sin_port = htons(nsock->args.listen_port);

	/*bind the socket*/
	if (bind(dgram->recv_fd, (struct sockaddr *)&dgram->recv_addr, sizeof (dgram->recv_addr)) < 0) {
		loge("recv_sock udp bind failed.\n");
        ret = -EINVAL;
        goto failed;
    }

	if((dgram->send_fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
		loge("create send socket fail. ret is %d\n", dgram->send_fd);
        ret = -EINVAL;
        goto failed;
    }

	optval = 1;
	if (setsockopt(dgram->send_fd, SOL_SOCKET, SO_REUSEADDR, 
                (const void *)&optval, sizeof(int)) < 0) {
		loge("send_sock udp setsockopt fail.\n");
        ret = -EINVAL;
        goto failed;
    }

	memset(&dgram->send_addr, 0, sizeof(struct sockaddr_in));   
	dgram->send_addr.sin_family = AF_INET;
	dgram->send_addr.sin_port = htons(nsock->args.dest_port);

	//if(!nsock->args.is_server)
		dgram->send_addr.sin_addr.s_addr = nsock->args.dest_ip;

	nsock->private_data = dgram;

#ifdef RECV_USE_CALLBACK
	if(nsock->args.recv_cb != NULL)
	{
		dgram->recv_running = 1;
		start_recv_th(nsock);	//start the receive thread when use callback receive the data.
	}
#endif

	logi("udp init success.\n");
	return 0;

failed:
    free(dgram);
    return ret;
}


#ifndef RECV_USE_CALLBACK	

/*udp library receive data function*/
/**
* @brief   dgram_recv
* 
* udp receive data function.
* @author Li_Xinhai
* @date 2012-06-26
* @param[in] arg1:input infomation structure.
* @return int return success or failed
* @retval returns zero on success
* @retval return a non-zero error code if failed
*/
static int dgram_recv(struct netsock* nsock, void* data, int len)
{
	int ret;
	struct sock_dgram* dgram = nsock->private_data;
	socklen_t cli_len = sizeof(struct sockaddr_in);

	ret = recvfrom(dgram->recv_fd, data, len, 0, (struct sockaddr *)&dgram->recv_addr, &cli_len);

	dgram->send_addr.sin_addr.s_addr = dgram->recv_addr.sin_addr.s_addr;//temp

	return ret;
}


/*udp library receive data function*/
/**
* @brief   dgram_recv_timeout
* 
* udp receive data function.
* @author Li_Xinhai
* @date 2012-06-26
* @param[in] arg1:input infomation structure.
* @return int return success or failed
* @retval returns zero on success
* @retval return a non-zero error code if failed
*/
static int dgram_recv_timeout(struct netsock* nsock, void* data, int len, unsigned long ms)
{
	int ret, nready;
	fd_set rset;
	struct timeval timeout;

	struct sock_dgram* dgram = nsock->private_data;
	socklen_t cli_len = sizeof(struct sockaddr_in);
	ret = -1;

	FD_ZERO(&rset);
	FD_SET(dgram->recv_fd, &rset);

	timeout.tv_sec = ms/1000;
	timeout.tv_usec = (ms%1000) * 1000;

	nready = select(dgram->recv_fd+1, &rset, NULL, NULL, &timeout);
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
		ret = recvfrom(dgram->recv_fd, data, len, 0, (struct sockaddr *)&dgram->recv_addr, &cli_len);
		dgram->send_addr.sin_addr.s_addr = dgram->recv_addr.sin_addr.s_addr;//temp
	}

	return ret;
}



#else	/*have definition the RECV_USE_CALLBACK macro*/

static int dgram_recv(struct netsock* nsock, void* data, int* len)
{
	return -1;
}

/*udp library receive data function*/
static int dgram_recv_timeout(struct netsock* nsock, void* data, int len, unsigned long timeout)
{
	return -1;
}



/**
* @brief   dgram_recv_thread
* 
* udp receive thread.
* @author Li_Xinhai
* @date 2012-06-26
* @param[in] arg1:input infomation structure.
* @return int return success or failed
* @retval returns zero on success
* @retval return a non-zero error code if failed
*/
static void* dgram_recv_thread(void* arg)
{
	int ret, nready;
	int err = 0;
	socklen_t cli_len = sizeof(struct sockaddr_in);
	struct net_packet pack;
	int buf_len;
	struct timeval timeout = {5,0};
	struct netsock *nsock = (struct netsock*)arg;
	struct sock_dgram *dgram = nsock->private_data;

	fd_set rset;
	pack.data = malloc(nsock->args.buf_size);

	while(dgram->recv_running)
	{		
		FD_ZERO(&rset);
		FD_SET(dgram->recv_fd, &rset);

		timeout.tv_sec = 5;
		timeout.tv_usec = 0;

		nready = select(dgram->recv_fd+1, &rset, NULL, NULL, &timeout);
		if(nready < 0)//select error.
		{
			if(nsock->args.err_cb)
				nsock->args.err_cb(E_SOCKSELECT);
			break;
		}
		else if(nready == 0)//select timeout.
		{
			err++;
			if(err > 5 && nsock->args.err_cb)
			{
				nsock->args.err_cb(E_SOCKTIMEOUT);
				break;
			}
			continue;
		}
		else if(FD_ISSET(dgram->recv_fd, &rset))
		{
			memset(pack.data, 0, sizeof(nsock->args.buf_size));
			if ((buf_len = recvfrom(dgram->recv_fd, pack.data, nsock->args.buf_size, 0, 
								(struct sockaddr *)&dgram->recv_addr, &cli_len)) < 0)
			{	//recv error
				if(nsock->args.err_cb)
					nsock->args.err_cb(E_SOCKRECV);
				break;
			}
			else
			{	//recv data success.
				logd("receive udp data, size=(%d)\n", buf_len);

				pack.datalen = buf_len;
				pack.conn.sock_addr = dgram->recv_addr;

				if(nsock->args.recv_cb)
					nsock->args.recv_cb(&pack);	//call the callback function.
			}
		}
	}

	free(pack.data); 
	return 0;
}


/*
*start the receive data thread.
*/
static void start_recv_th(struct netsock* nsock)
{
	int ret;
	pthread_t recv_th;

	ret = pthread_create(&recv_th, NULL, dgram_recv_thread, nsock);
	if(ret)
	{
		loge("create the recv thread error!\n");
		exit(EXIT_FAILURE);
	}
}

#endif


/**
* @brief   dgram_send
* 
* dgram_send
* @author Li_Xinhai
* @date 2012-06-26
* @param[in] arg1: send data.
* @param[in] arg1: send lenght.
* @return int return success or failed
* @retval returns zero on success
* @retval return a non-zero error code if failed
*/
static int dgram_send(struct netsock* nsock, void* data, int len)
{
	struct sock_dgram* dgram = nsock->private_data;

	if(sendto(dgram->send_fd, data, len, 0, (struct sockaddr *)&dgram->send_addr, 
				sizeof(struct sockaddr_in)) < 0)
		return -1;

	return 0;
}


/**
* @brief   dgram_serv_send
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
int dgram_serv_send(struct netsock *nsock, void *session, void *buf, int len)
{
	struct sock_dgram* dgram = nsock->private_data;
	struct connection* conn = (struct connection*)session;

	if(sendto(dgram->send_fd, buf, len, 0, (struct sockaddr *)&conn->sock_addr, 
				sizeof(struct sockaddr_in)) < 0)
		return -1;
	
	return 0;
}



/**
* @brief   dgram_release
* 
* call this function destory the resource.
* @author Li_Xinhai
* @date 2012-06-26
* @param[in] arg1:input infomation structure.
* @return int return success or failed
* @retval returns zero on success
* @retval return a non-zero error code if failed
*/
static void dgram_release(struct netsock* nsock)
{
	struct sock_dgram* dgram = nsock->private_data;

	close(dgram->recv_fd);
	close(dgram->send_fd);

	free(dgram);
}


struct netsock_operations dgram_ops = {
    .init       = dgram_init,
	.release    = dgram_release,
	.send       = dgram_send,
	.serv_send  = dgram_serv_send,
	.recv       = dgram_recv,
	.recv_timeout = dgram_recv_timeout,
};




