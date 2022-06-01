#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <stdio.h>

fd_set fds;
fd_set readFds;
fd_set writeFds;
char msgRecv[1024] = {0};
char *sendMsg = NULL;

int socket_fd = -1;
int id = 0;

typedef struct s_client {
	int fd;
	int id;
	struct s_client *next;
}t_client;

t_client *g_client = NULL;

int intlen(int i) {
	int len = 0;

	if(i == 0)
		return 1;
	while(i > 0) {
		i = i / 10;
		len++;
	}
	return len;

}

int getId(int fd){
	t_client *tmp = g_client;

	while(tmp != NULL){
		if (tmp->fd == fd)
			return (tmp->id);
		tmp = tmp->next;
	} 
	return -1;
}


void freelist(){
	t_client *tmp = g_client;
	t_client *tofree = NULL;

	while(tmp!=NULL) {
		tofree = tmp;
		tmp = tmp->next;
		if(tmp->fd != -1){
			close(tmp->fd);
			FD_CLR(tmp->fd, &fds);
		}
		free(tofree);
		tofree = NULL;
	}

}

void exit_error(){
	const char *msg = "Fatal error\n";
	write(STDERR_FILENO, msg, strlen(msg));
	if (socket_fd != -1){
		close(socket_fd);
		FD_CLR(socket_fd, &fds);
	}
	freelist();
	if(sendMsg != NULL){
		free(sendMsg);
		sendMsg = NULL;
	}
	exit(1);
}

void deleteClient(int fd){
	t_client *tmp = g_client;
	t_client *tofree = NULL;

	if (g_client != NULL && g_client->fd == fd) {
		tofree = g_client;
		g_client = g_client->next;
	}
	else {
		while(tmp != NULL && tmp->next != NULL && tmp->next->fd != fd )
			tmp = tmp->next;
		tofree = tmp->next;
		tmp->next = tmp->next->next;
	}
	free(tofree);
	tofree = NULL;
}

void addToList(t_client *newclient) {
	t_client *tmp = g_client;
	
	if(g_client == NULL)
		g_client = newclient;
	else  {
		while(tmp != NULL && tmp->next != NULL)
			tmp = tmp->next;
		tmp->next = newclient;   //new add to tmp->next
	}
}

void sendToAll(int fd, char *msg) {
	t_client *tmp = g_client;
	int ret;

	while(tmp != NULL) {
		if (tmp->fd != fd)
			ret = send(tmp->fd, msg, strlen(msg), 0);
		tmp = tmp->next;
	}
	free(msg); //!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!dont forget free msg;
	msg = NULL;

}

void clientConn() {  //newcalloc initfdid addtolist callocmsg send
	int clientFd = -1;
	t_client *newclient = NULL;
	struct sockaddr_in c_addr;
	char *msg = NULL;	

        socklen_t len = sizeof(c_addr);
        clientFd = accept(socket_fd, (struct sockaddr *)&c_addr, &len);
	newclient = calloc(1, sizeof(t_client));
	if (newclient == NULL)
		exit_error();
	newclient->id = id++;
	newclient->fd = clientFd;

	addToList(newclient);	
	FD_SET(clientFd, &fds);
	
	printf("!!!!!!!intlen id: %d\n", newclient->id);
	msg = calloc(intlen(newclient->id) + sizeof("server: client  just arrived\n"), sizeof(char));
	if (msg == NULL) 
		exit_error();
	sprintf(msg, "server: client %d just arrived\n", newclient->id);
	sendToAll(clientFd, msg);

}


void clientDisConn(int fd) {
	char *msg = NULL;

	msg = calloc(intlen(getId(fd)) + sizeof("server: client  just left\n"), sizeof(char));
	if (msg == NULL)
		exit_error();
	sprintf(msg, "server: client %d just left\n", getId(fd));
	sendToAll(fd, msg);
	close(fd);
	FD_CLR(fd, &fds);
	deleteClient(fd);
}


void serverInit(int port) {
	struct sockaddr_in s_addr;	

	socket_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (socket_fd == -1)
                exit_error();

        bzero(&s_addr, sizeof(s_addr));

        s_addr.sin_family = AF_INET;
        s_addr.sin_addr.s_addr = htonl(2130706433); //127.0.0.1
        s_addr.sin_port = htons(port);

        // Binding newly created socket to given IP and verification 
        if ((bind(socket_fd, (const struct sockaddr *)&s_addr, sizeof(s_addr))) != 0)
                exit_error();
      
        if (listen(socket_fd, SOMAXCONN) != 0)
                exit_error();

}


int maxFd() {
	t_client *tmp = g_client;
	int max = socket_fd;

	while(tmp != NULL){
		if(tmp->fd > max)
			max = tmp->fd;
		tmp = tmp->next;
	}
	return max;
}

int extract_message(char **buf, char **msg)
{
        char    *newbuf;
        int     i;

        *msg = 0;
        if (*buf == 0)
                return (0);
        i = 0;
        while ((*buf)[i])
        {
                if ((*buf)[i] == '\n')
                {
                        newbuf = calloc(1, sizeof(*newbuf) * (strlen(*buf + i + 1) + 1));
                        if (newbuf == NULL)
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
        char    *newbuf;
        int             len;

        if (buf == 0)
                len = 0;
        else
                len = strlen(buf);
        newbuf = malloc(sizeof(*newbuf) * (len + strlen(add) + 1));
        if (newbuf == NULL)
		exit_error();
        newbuf[0] = 0;
        if (buf != 0)
                strcat(newbuf, buf);
        free(buf);
        strcat(newbuf, add);
        return (newbuf);
}

//"client %d: "
void handleReq(int fd){
        char *line = NULL;
        char *sendline = NULL;

        sendMsg = str_join(sendMsg, msgRecv);  //
        while(extract_message(&sendMsg, &line) == 1){
                sendline = calloc(intlen(getId(fd)) + sizeof("client : ") + strlen(line), sizeof(char));
                if (sendline == NULL) {
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

void serverStart() {
        FD_ZERO(&fds);
        FD_SET(socket_fd, &fds);

        for(;;) {
                writeFds = readFds = fds;

                int event = select(maxFd()+1, &readFds, &writeFds, NULL, NULL);
                if (event < 0)
                        continue;
                for(int fd = 0; fd <= maxFd(); fd++){
                        if (FD_ISSET(fd, &readFds)){
                                if (fd == socket_fd){
                                        clientConn();
                                        break;
                                }
                                else {
                                        bzero(msgRecv, sizeof(msgRecv));
                                        int ret = recv(fd, msgRecv, 1024, 0);
                                        if (ret <= 0){
                                                clientDisConn(fd);
                                                break;
                                        }
                                        else
                                                handleReq(fd);
                         
                                }
                        }
                }
        }
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
