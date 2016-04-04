#include "csapp.h"

/*
 * Function prototypes
 */
void parse_url(char *ur, char *hostname, char *query_path, int *port);
void format_log_entry(char *logstring, struct sockaddr_in *sockaddr, char *uri, int size);
void *thread(void *arg);
void client_error(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg);
int connect_server(char *hostname, int port, char *path );
/*
 * varibles
 */
struct args{
    int fd;
    struct sockaddr_in sockaddr;
};
pthread_mutex_t mutex;
FILE* logfile;
/* 
 * main - Main routine for the proxy program 
 */
int main(int argc, char **argv)
{
    FILE* logfile;
    pthread_t tid;
    /* Check arguments */
    if (argc != 2) {
	fprintf(stderr, "Usage: %s <port number>\n", argv[0]);
	exit(0);
    }

    Signal(SIGPIPE, SIG_IGN);
   
    logfile = Fopen("./logfile", "a");
    int listenfd = Open_listenfd( atoi(argv[1]) );
    while(1){
        socklen_t len = sizeof(int);
        struct args* p = (struct args*)Malloc(sizeof(struct args));
        p->fd = Accept(listenfd, (SA*)(&p->sockaddr),&len);  
        Pthread_create(&tid, NULL, thread, p);
    }
    exit(1);
}

void *thread(void *arg)
{
     rio_t rp;
     char buf[MAXLINE];
     char method[MAXLINE];
     char url[MAXLINE];
     char version[MAXLINE];
    
     char hostname[MAXLINE];
     char path[MAXLINE]; 
     int port;
     int serverfd;
     int clength;

     Pthread_detach(Pthread_self());
     struct args* tmp = (struct args*)arg;
     int fd =  tmp->fd;
     
     struct sockaddr_in sockaddr = tmp->sockaddr;
     Free(tmp);
     
     Rio_readinitb(&rp, fd); 
     Rio_readlineb(&rp, buf, MAXLINE);

    if(sscanf(buf, "%s %s %s", method, url, version) < 3){
        fprintf(stderr, "sscanf error");
        client_error(fd, method, "404","Not Found", "Not Found");  
        Close(fd);
        return NULL;
    }
   
    if(strcmp(method,"GET")){
        fprintf(stderr, "error request");
        client_error(fd, method, "500","Not Implement", "Not Implement");  
        Close(fd);
        return NULL;
    }
    /*忽略首部和实体*/
    do{
        Rio_readlineb(&rp, buf, MAXLINE);    
    }while(strcmp(buf, "\r\n"));
    
    /*解析URL，请求服务器*/
    parse_url(url,hostname, path, &port); 
    if( (serverfd = connect_server(hostname,port, path))  < 0){
        Close(fd);
        return NULL;
    }
    
    /*等待读取服务器响应，并送回请求客户端*/
    do{
       Rio_readinitb(&rp, serverfd);
       Rio_readlineb(&rp, buf, MAXLINE);
       
       if(strstr(buf, "Content-length:")){
            sscanf(buf, "Content-length: %d\r\n", &clength);
       Rio_writen(fd, buf, MAXLINE);
       }
      }while(strcmp(buf,"\r\n"));
     
    
       char logstring[MAXLINE]; 
       pthread_mutex_lock(&mutex);
       format_log_entry(logstring, &sockaddr, url, clength);
       pthread_mutex_unlock(&mutex);
       fprintf(logfile, "%s\n", logstring);
       fflush(logfile);
       close(fd);
       close(serverfd);

}

int connect_server(char *hostname, int port, char *path )
{
    static const char *user_agent = "User-Agent: Mozilla (X11; Linux i386; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";
    static const char *accept_str= "Accept: text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8\r\nAccept-Encoding: gzip, deflate\r\n";
    static const char *connection = "Connection: close\r\nProxy-Connection: close\r\n";
    
    char buf[MAXLINE];
    /* connect to server */
    int proxy_clientfd;
    proxy_clientfd=open_clientfd(hostname,port);

    /* if failed return */
    if(proxy_clientfd<0)
        return proxy_clientfd;

    /* write request to server */
    sprintf(buf,"GET %s HTTP/1.0\r\n", path);
    Rio_writen(proxy_clientfd,buf,strlen(buf));
    sprintf(buf,"Host: %s\r\n",hostname);
    Rio_writen(proxy_clientfd,buf,strlen(buf));
    Rio_writen(proxy_clientfd,user_agent,strlen(user_agent));
    Rio_writen(proxy_clientfd,accept_str,strlen(accept_str));
    Rio_writen(proxy_clientfd,connection,strlen(connection));
    Rio_writen(proxy_clientfd,"\r\n",strlen("\r\n"));
    printf("request to server is done.");
    return proxy_clientfd;
}

/* parse request url */
void parse_url(char *ur, char *hostname, char *query_path, int *port)
{
    char url[100];
    url[0]='\0';
    strcat(url,ur);
    hostname[0]=query_path[0]='\0';
    char *p=strstr(url,"//");        /* skip "http://" and "https://" */
    if(p!=NULL) {
        p=p+2;
    } else {
        p=url;
    }
    char *q=strstr(p,":");            /* read ":<port>" and "/index.html" */
    if(q!=NULL) {
        *q='\0';
        sscanf(p,"%s",hostname);
        sscanf(q+1,"%d%s",port,query_path);
    } else {
        q=strstr(p,"/");
        if(q!=NULL) {
            *q='\0';
            sscanf(p,"%s",hostname);
            *q='/';
            sscanf(q,"%s",query_path);
        } else {
            sscanf(p,"%s",hostname);
        }
        *port=80;
    }
    /* the default path */
    if(strlen(query_path)<=1)
        strcpy(query_path,"/index.html");

    return;
}

/*
 * format_log_entry - Create a formatted log entry in logstring. 
 * 
 * The inputs are the socket address of the requesting client
 * (sockaddr), the URI from the request (uri), and the size in bytes
 * of the response from the server (size).
 */
void format_log_entry(char *logstring, struct sockaddr_in *sockaddr, 
		      char *uri, int size)
{
    time_t now;
    char time_str[MAXLINE];
    unsigned long host;
    unsigned char a, b, c, d;

    /* Get a formatted time string */
    now = time(NULL);
    strftime(time_str, MAXLINE, "%a %d %b %Y %H:%M:%S %Z", localtime(&now));

    /* 
     * Convert the IP address in network byte order to dotted decimal
     * form. Note that we could have used inet_ntoa, but chose not to
     * because inet_ntoa is a Class 3 thread unsafe function that
     * returns a pointer to a static variable (Ch 13, CS:APP).
     */
    host = ntohl(sockaddr->sin_addr.s_addr);
    a = host >> 24;
    b = (host >> 16) & 0xff;
    c = (host >> 8) & 0xff;
    d = host & 0xff;


    /* Return the formatted log entry string */
    sprintf(logstring, "%s: %d.%d.%d.%d %s", time_str, a, b, c, d, uri);
}

void client_error(int fd, char *cause, char *errnum, 
                 char *shortmsg, char *longmsg) 
{
    char buf[MAXLINE], body[MAXBUF];

    /* Build the HTTP response body */
    sprintf(body, "%s: %s\r\n", errnum, shortmsg);
    sprintf(body, "%s%s: %s", body, longmsg, cause);

    /* Print the HTTP response */
    sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-type: text/html\r\n");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
    Rio_writen(fd, buf, strlen(buf));
    Rio_writen(fd, body, strlen(body));
}

