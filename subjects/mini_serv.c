#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>

fd_set fds;
fd_set readFds;
fd_set writeFds;

int socket_fd;
char msgRecv[4096] = {0};
char *msgSend = NULL;
int id = 0;

typedef struct s_client {
	int id;
	int fd;
	struct s_client *next;
} t_client;

t_client *g_client = NULL;

void freeList() {
	t_client *tmp = g_client;
	t_client *waitfree = NULL;

	while (tmp != NULL) {  //
		waitfree = tmp;
		tmp = tmp->next;
		if (tmp->fd != -1) {
			close(tmp->fd);
			FD_CLR(tmp->fd, &fds);
		}
		free(waitfree);
		waitfree = NULL;
	}
	close(socket_fd);
	FD_CLR(socket_fd, &fds);
}

void exit_error() {
	const char *msg = "Fatal error\n";

	write(STDERR_FILENO, msg, strlen(msg));
	if (socket_fd != -1) {
		close(socket_fd);
		FD_CLR(socket_fd, &fds);
	}
	freeList();
	if (msgSend != NULL){
		free(msgSend);
		msgSend = NULL;
	}
	exit(1);
}


int intlen(int i) {
	int len = 0;

	if (i == 0)
		return 1;
	while(i > 0){
		i = i / 10;
		++len;
	}
	return len;
}


int extract_message(char **buf, char **msg)  //找到\n 
{
	char	*newbuf;
	int	i;

	*msg = 0;
	if (*buf == 0)
		return (0);
	i = 0;
	while ((*buf)[i])
	{
		if ((*buf)[i] == '\n')
		{
			newbuf = calloc(1, sizeof(*newbuf) * (strlen(*buf + i + 1) + 1));
			if (newbuf == 0)
				exit_error();
			strcpy(newbuf, *buf + i + 1);
			*msg = *buf;
			(*msg)[i + 1] = 0;
			*buf = newbuf;
			return (1);
		}
		i++;
	}
	return (0);
}

char *str_join(char *buf, char *add)
{
	char	*newbuf;
	int		len;

	if (buf == 0)
		len = 0;
	else
		len = strlen(buf);
	newbuf = malloc(sizeof(*newbuf) * (len + strlen(add) + 1));
	if (newbuf == NULL)
		exit_error();
	newbuf[0] = '\0';
	if (buf != NULL)
		strcat(newbuf, buf);
	free(buf);
	strcat(newbuf, add);
	return (newbuf);
}

void serverInit(int port) {
	struct  sockaddr_in s_addr;

    socket_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (socket_fd == -1) {
        exit_error();
	}
	bzero(&s_addr, sizeof(s_addr));

    s_addr.sin_family = AF_INET;
	s_addr.sin_addr.s_addr = htonl(2130706433);
    s_addr.sin_port = htons(port);
   
	if (fcntl(socket_fd, F_SETFL, O_NONBLOCK) == -1){
        exit_error();
    }
	
    if ((bind(socket_fd, (const struct sockaddr *)&s_addr, sizeof(s_addr))) != 0) {
        exit_error();
	}
    if (listen(socket_fd, SOMAXCONN) != 0) {
        exit_error();
	}
}

int max_fd() {
	
	int max = socket_fd;
	t_client *tmp = g_client;
	
	while(tmp) {
		if (tmp->fd > max) 
			max = tmp->fd;
		tmp = tmp->next;
	}
	return max;
}

void addToList(t_client *newclient) {
	t_client *tmp = g_client;

	if (tmp == NULL) {
		g_client = newclient;
	}
	else {
		while (tmp != NULL && tmp->next != NULL)  //走到最后
			tmp = tmp->next;
		tmp->next = newclient;
	}
	
}

void sendToAll(int fd, char *msg) {
	t_client *tmp = g_client;
	int ret;

	while(tmp) {
		if (tmp->fd != fd) {
			ret = send(tmp->fd, msg, strlen(msg), 0);
			if (ret == -1) {
				free(msg);
				msg = NULL;
				exit_error();
			}
		}
		tmp = tmp->next;
	}
	free(msg);
	msg=NULL;

}

void clientConn() {
	int newclientFd = -1;
	struct sockaddr_in	c_Addr;

	t_client *newClient = NULL;
	char *msg = NULL;

	socklen_t len = sizeof(c_Addr); // socklen_tq
	newclientFd = accept(socket_fd, (struct sockaddr *)&c_Addr, &len);  //accept 不掉线
	
	newClient = calloc(1, sizeof(t_client));
	if (newClient == NULL)
		 exit_error();
	newClient->id = id++;
	newClient->fd = newclientFd;
	//add to list;
	addToList(newClient);

	FD_SET(newclientFd, &fds);
	msg = calloc(intlen(newClient->id) + sizeof("server: client  just arrived\n"), sizeof(char));
	if (msg == NULL)
		exit_error();
	sprintf(msg, "server: client %d just arrived\n", newClient->id);
	sendToAll(newclientFd, msg);  //连接信息给所有人v
}


int getId(int fd) {

	t_client *tmp = g_client;

	while(tmp) {
		if (tmp->fd == fd) {
			return (tmp->id);
		}
		tmp = tmp->next;
	}
	return -1;
}

void deletClient(int fd) {
	t_client *tmp = g_client;
	t_client *todelet = NULL;
	
	if (g_client != NULL && g_client->fd == fd) {  //是头
		todelet = g_client;
		g_client = g_client->next;
	}
	else {
		while(tmp != NULL && tmp->next != NULL && tmp->next->fd != fd) //找到阶段
			tmp = tmp->next;
		
		todelet = tmp->next;
		tmp->next = tmp->next->next;
	}
	free(todelet);
	todelet = NULL;
}

void clientDisconn(int fd) {
	char *msg = NULL;

	msg = calloc(intlen(getId(fd)) + sizeof("server: client  just left\n"), sizeof(char));
	if (msg == NULL)
		exit_error();
	sprintf(msg, "server: client %d just left\n", getId(fd));

	sendToAll(fd, msg);   //发送离开信息给所有人

	close(fd);   //关闭fd
	FD_CLR(fd, &fds);   //删除fd

	deletClient(fd);  //删除client
}


void handleMsg(int fd) {
	char *line = NULL;
	char *sendline = NULL;

	msgSend = str_join(msgSend, msgRecv); //把所有接受的消息都有连接起来
	// printf("msgSend : %s\n", msgSend);
	while(extract_message(&msgSend, &line) == 1) {
		sendline = calloc(intlen(getId(fd)) + sizeof( "client : ") + strlen(line), sizeof(char)); //每一行 :client id : + msg
		if(sendline == NULL) {
			free(line);
			line = NULL;
			exit_error();
		}
		sprintf(sendline, "client %d: %s", getId(fd), line);  //把字符串放进去
		// printf("sendline : %s\n", sendline);
		free(line);
		line = NULL;
		sendToAll(fd, sendline);
	}

}

void serverStart() {
	FD_ZERO(&fds);
	FD_SET(socket_fd, &fds);


	for(;;) {

		writeFds = readFds = fds;
		int max = max_fd() + 1;
		
		int event = select(max, &readFds, &writeFds, NULL, NULL);
		
		if (event < 0) {
		
			continue ;
		}
		for (int i = 0; i <= max_fd(); i++) {
			if (FD_ISSET(i, &readFds)) {    // 指定的文件描述符是否在该集合中。
				if (i == socket_fd) { //connect:new client connect 
					clientConn();
					break ;
				}
				else {   
					bzero(msgRecv, sizeof(msgRecv)); //初始化msgRevu 
					int ret = recv(i, msgRecv, 1024, 0);  //recv (fd, this->buff, MAX_BUFF, 0)
					if (ret <= 0) {
						clientDisconn(i);
						break;
					}
					else {
						handleMsg(i); 
					}
				}
			}
		}
	}
}


int main(int ac, char **av) {
	const char *msg = "Wrong number of arguments\n";

	if (ac != 2) {
		write(2, msg, strlen(msg));
		exit (1);
	}
	
	serverInit(atoi(av[1]));

	serverStart();
	return 0;
	
}