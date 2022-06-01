#include <string.h>
#include <unistd.h>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/select.h>


fd_set fds;
fd_set readFds;
fd_set writeFds;

int socket_fd;
int id = 0;
char msgRecv[4096] = {0};
char *sendMsg = NULL;

typedef struct s_client {
	int fd;
	int id;
	struct s_client *next;
}t_client;

t_client *g_client = NULL;

int extract_message(char **buf, char **msg)
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
				return (-1);
			strcpy(newbuf, *buf + i + 1);  //到\n的buff
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
	if (newbuf == 0)
		return (0);
	newbuf[0] = 0;
	if (buf != 0)
		strcat(newbuf, buf);
	free(buf);
	strcat(newbuf, add);
	return (newbuf);
}

int intlen(int i) {
	int len = 0;
	if (i == 0) 
		return 1;
	while(i > 0) {
		i = i / 10;
		len++;
	}
	return len;
}

void freelist() {
	t_client *tmp = g_client;
	t_client *tofree = NULL;

	while (tmp != NULL) {
		tofree = tmp;
		tmp = tmp->next;
		if (tmp->fd != -1) {
			close(tmp->fd);
			FD_CLR(tmp->fd, &fds);
		}
		free(tofree);
		tofree = NULL;
	}
}

void exit_error() {  //FREE MSG
	const char *msg =  "Fatal error\n";
	write(STDERR_FILENO, msg, strlen(msg));
	if (socket_fd != -1) {
		close(socket_fd);
		FD_CLR(socket_fd, &fds);
	}
	freelist();
	if (sendMsg != NULL) {
		free(sendMsg);
		sendMsg = NULL;
	}
	exit(1);
}


void sendToAll(int fd, char *msg) {
	t_client *tmp = g_client;
	int ret = -1;

	while(tmp != NULL) {
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
	msg = NULL;
}


void addtolist(t_client *newClient) {
	t_client *tmp = g_client;
	
	if(g_client == NULL) 
		g_client = newClient;

	else {
		while(tmp != NULL && tmp->next != NULL) //当前有数并且下一个也有数 一直走
			tmp = tmp->next;
		tmp->next = newClient;    //当前有数 下一个没数的时候停.下个数 放进去
	}
}

int getId(int fd) {
	t_client *tmp = g_client;

	while(tmp != NULL) {
		if(tmp->fd == fd) {
			return (tmp->id);
		}
		tmp = tmp->next;
	}
	return -1;
}

int maxFd() {
	t_client *tmp = g_client;
	int max = socket_fd;
	while(tmp != NULL) {
		if (tmp->fd > max)
			max = tmp->fd;
		tmp = tmp->next;
	}
	return max;

}


void deleteClient(int fd) {  // 链表去除 找到前一位 
	t_client *tmp = g_client;
	t_client *todelete = NULL;

	if (g_client != NULL && g_client->fd == fd) { //first
		todelete = g_client;
		g_client = g_client->next;
	}
	else {
		while(tmp != NULL && tmp->next != NULL && tmp->next->fd != fd) //走到要删除的前一位
			tmp = tmp->next;

		todelete = tmp->next;
		tmp->next = tmp->next->next;
	}
	free(todelete);
	todelete = NULL;
}

void clientDisconnect(int fd) {//先写信息 再发送信息 然后关闭 删除fd 最后delet
	char *leftmsg = NULL;
	
	//"server: client %d just left\n"
	leftmsg = calloc(intlen(getId(fd)) + sizeof("server: client  just left\n"), sizeof(char));
	if (leftmsg == NULL)
		exit_error();
	sprintf(leftmsg, "server: client %d just left\n", getId(fd));
	sendToAll(fd, leftmsg);
	close(fd);
	FD_CLR(fd, &fds);
	deleteClient(fd);
} 



void clientConnect() {//c_addr accept calloc addtolist FD_EST 发送信息(arrived) 
	struct sockaddr_in c_addr;
	int clientFd = -1;

	t_client *newClient = NULL;
	socklen_t len = sizeof(c_addr);
	char *welcomemsg = NULL;

	clientFd = accept(socket_fd, (struct sockaddr *)&c_addr, &len);

	newClient = calloc(1, sizeof(t_client));
	if (newClient == NULL)
		exit_error();
	
	newClient->fd = clientFd;
	newClient->id = id++;

	addtolist(newClient);
	FD_SET(clientFd, &fds);
	//"server: client %d just arrived\n"
	welcomemsg = calloc(intlen(newClient->id) + sizeof("server: client  just arrived\n"), sizeof(char));
	if (welcomemsg == NULL) 
		exit_error();

	sprintf(welcomemsg, "server: client %d just arrived\n", newClient->id);
	sendToAll(clientFd, welcomemsg);
} 




void handleMsg(int fd) {  //join calloc sprintf free sendto all
	char *line = NULL;
	char *sendline = NULL;

	sendMsg = str_join(sendMsg, msgRecv);
	while(extract_message(&sendMsg, &line) == 1) {
		sendline = calloc(intlen(getId(fd)) + sizeof("client : ") + strlen(line), sizeof(char));
		if(sendline == NULL) {
			free(line);
			line = NULL;
			exit_error();
		}
		sprintf(sendline, "client %d: %s", getId(fd), line);
		free(line);
		line = NULL;
		sendToAll(fd, sendline);

	}

}

void serverStart() {  //for 开始, select FD_ISSET 
	FD_ZERO(&fds);
	FD_SET(socket_fd, &fds);

	for(;;) {
		writeFds = readFds = fds;

		int event = select(maxFd() + 1, &readFds, &writeFds, NULL, NULL);
		if (event == -1) 
			continue;
		for(int fd = 0; fd <= maxFd() ; fd++) {
			if (FD_ISSET(fd, &readFds)) {
				if (fd == socket_fd) {
					clientConnect();
					break;
				}
				else {   //已经连接 接收
					bzero(msgRecv, sizeof(msgRecv));
					int ret = recv(fd, msgRecv, 4096, 0);
					if (ret <= 0) {
						clientDisconnect(fd);
						break;
					}
					else {
						printf("!!!!!!!!!\n");
						handleMsg(fd);
					}
				}
			}
		}
	}
}

void serverInit(int port){  //socket s_addr
	struct  sockaddr_in s_addr;

	socket_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (socket_fd == -1)
		exit_error();
	
	bzero(&s_addr, sizeof(s_addr));
	s_addr.sin_family = AF_INET; 
	s_addr.sin_addr.s_addr = htonl(2130706433); //127.0.0.1
	s_addr.sin_port = htons(port); 

  	if ((bind(socket_fd, (const struct sockaddr *)&s_addr, sizeof(s_addr))) != 0)
		exit_error();
	if (listen(socket_fd, 256) != 0) 	
		exit_error();

}

int main(int ac, char **av) {
	const char *msg = "Wrong number of arguments\n";

	if (ac != 2) {
		write(STDERR_FILENO, msg, strlen(msg));
		exit(1);
	}
	serverInit(atoi(av[1]));

	serverStart();
	return 0;
}



