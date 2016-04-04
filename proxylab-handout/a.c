/*
 * justforyeah@yeah.net
 * 2014.12.28 18:24
 */

#include "csapp.h"

#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* argument for each thread */
struct arg {
    int connfd;            /* client socked */
    int turn;            /* the time of current request */
};
/* cache block */
struct cache_block {
    char data[MAX_OBJECT_SIZE];        /* store the response head and body */
    sem_t mutex;                    /* 模仿书上P673,使用写者-读者模型解决并发冲突 */
    sem_t w;
    int readcnt;

    int turn;                        /* the time of request */
    sem_t t_mutex;                
    sem_t t_w;
    int t_readcnt;

    char url[300];                    /* the url of request */
    sem_t url_mutex;
    sem_t url_w;
    int url_readcnt;

};
/* total 10 cache block */
struct cache_block cache[10];

/* init the block */
void cache_erase(int index);

/* because of concurrency, all operation on cache use following 4 function */
/* write data, url and turn on cache[index] */
void cache_write(int index, char *url,char *data, int turn);
/* read data on cache[index] */
void cache_data_read(int index, char *dst, int turn);
/* read url on cache[index] */
void cache_url_read(int index,char *dst);
/* read turn on cache[index] */
int cache_turn_read(int index);

/* thread for each request */
void *thread(void *connfdp);

/* parse the request line */
void parse_url(char *url, char *hostname, char *query_path, int *port);

/* connect to server, if failed return -1 */
int connect_server(char *hostname,int port,char *query_path);


int main(int argc,char **argv)
{
    printf("Begin:\n");
    Signal(SIGPIPE, SIG_IGN);
    struct sockaddr_in clientaddr;
    int port,listenfd,clientlen;
    int turn=1;
    pthread_t tid;
    struct arg *p;

    /* check port */
    if(argc!=2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(0);
    }

    // init 10 cache blocks
    int i;
    for(i=0;i<10;i++)
        cache_erase(i);

    port=atoi(argv[1]);
    listenfd=Open_listenfd(port);
    clientlen=sizeof(clientaddr);

    while(1) {
        p=(int*)Malloc(sizeof(struct arg));
        p->connfd=Accept(listenfd,(SA*)&clientaddr,&clientlen);
        p->turn =turn++;
        /* create thread */
        printf("new connection is coming.");
        Pthread_create(&tid,NULL,thread,(void *)p);
    }
    return 0;
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

/* connect to server and return socket fd */
int connect_server(char *hostname,int port,char *query_path)
{
    static const char *user_agent = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";
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
    sprintf(buf,"GET %s HTTP/1.0\r\n",query_path);
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

/*
 * if there is a finished cache, read and response.
 * else connect to server
 */
void *thread(void *p)
{
    Pthread_detach(pthread_self());
    int connfd=((struct arg*)p)->connfd,turn=((struct arg*)p)->turn;
    free(p);

    char buf[MAXLINE];
    char method[MAXLINE],version[MAXLINE],url[MAXLINE];
    char host[MAXLINE],query[MAXLINE];
    char url_tmp[300],*data_tmp;
    rio_t rio;
    int index,port,content_length;
    int serverfd;

    /* read request line */
    rio_readinitb(&rio,connfd);
    rio_readlineb(&rio,buf,MAXLINE);
    sscanf(buf,"%s %s %s",method,url,version);
    printf("Parse Client: method:%s, url:%s, version:%s", method, url, version);
    if(strcasecmp(method,"GET")) {
        /* ignore */
        printf("Not GET\r\n");
        Close(connfd);
        return NULL;
    }
    /* ignore client request */
    do {
        rio_readlineb(&rio,buf,MAXLINE-1);
    }while(strcmp(buf,"\r\n"));

    /* find cache block */
    for(index=0;index<10;index++) {
        cache_url_read(index,url_tmp);
        /* the block'url is same as current url */
        if(!strcmp(url,url_tmp))
            break;
    }

    data_tmp=(char*)Malloc(MAX_OBJECT_SIZE);
    data_tmp[0]='\0';

    if(index <10) { /* if have cached */
        cache_data_read(index,data_tmp,turn);
        rio_writen(connfd,data_tmp,strlen(data_tmp));
        Close(connfd);
        free(data_tmp);
        return NULL;
    }

    /* connect to server */
    parse_url(url,host,query,&port);
    if((serverfd=connect_server(host,port,query))<0) {
        /* connect to server failed, return */
        free(data_tmp);
        Close(connfd);
        return NULL;
    }
    printf("Read reply from server,then write to client.");
    rio_readinitb(&rio,serverfd);
    content_length=0;
    /* read response head line */
    do {
        int t=rio_readlineb(&rio,buf,MAXLINE-1);
        if(t<=0)
            break;
        strncat(data_tmp,buf,t);
        if(strstr(buf,"Content-length")!=NULL)
            sscanf(buf,"Content-length: %d\r\n",&content_length);
        rio_writen(connfd,buf,t);
    }while(strcmp(buf,"\r\n"));

    /* read response body */
    /* response is small enough to cache */
    if(content_length+strlen(data_tmp)<MAX_OBJECT_SIZE) {
        while(content_length>0) {
            int t= rio_readnb(&rio,buf,(content_length<MAXLINE-1)?content_length:MAXLINE-1);
            if(t<=0)
                continue;
            content_length-=t;
            strncat(data_tmp,buf,t);
            rio_writen(connfd,buf,t);
        }
        index=0;
        int i;
        /* least-recently-used */
        for(i=1;i<10;i++) {
            if(cache_turn_read(i)<cache_turn_read(index)) {
                index=i;
            }
        }
        /* cache write */
        cache_write(index,url,data_tmp,turn);
    }
    /* ignore store and write to client */
    else {
        while(content_length>0) {
            int t= rio_readnb(&rio,buf,(content_length<MAXLINE-1)?content_length:MAXLINE-1);
            if(t<=0)
                break;
            content_length-=t;
            rio_writen(connfd,buf,t);
        }
    }
    Close(connfd);
    Close(serverfd);
    free(data_tmp);
    return NULL;
}

void cache_erase(int index)
{
    /* init all var */
    cache[index].turn=0;    
    cache[index].url[0]='\0';
    cache[index].data[0]='\0';
    Sem_init(&cache[index].t_mutex,0,1);
    Sem_init(&cache[index].t_w,0,1);
    cache[index].t_readcnt=0;
    Sem_init(&cache[index].url_mutex,0,1);
    Sem_init(&cache[index].url_w,0,1);
    cache[index].url_readcnt=0;
    Sem_init(&cache[index].mutex,0,1);
    Sem_init(&cache[index].w,0,1);
    cache[index].readcnt=0;
}

void cache_write(int index,char *url, char *data, int turn)
{
    /* semaphore */
    P(&cache[index].url_w);
    P(&cache[index].w);
    P(&cache[index].t_w);
    /* begin write operation */
    cache[index].turn=turn;
    strcpy(cache[index].data,data);
    strcpy(cache[index].url,url);
    /* end write operation */

    /* semaphore */
    V(&cache[index].t_w);
    V(&cache[index].w);
    V(&cache[index].url_w);
    return ;
}

void cache_data_read(int index, char *dst, int turn)
{
    /* semaphore */
    P(&cache[index].mutex);
    cache[index].readcnt++;
    if(cache[index].readcnt==1)
        P(&cache[index].w);
    V(&cache[index].mutex);
    P(&cache[index].t_w);

    /* begin read operation */
    cache[index].turn=turn;
    strcpy(dst,cache[index].data);
    /* end read operation */

    /* semphore */
    V(&cache[index].t_w);
    P(&cache[index].mutex);
    cache[index].readcnt--;
    if(cache[index].readcnt==0)
        V(&cache[index].w);
    V(&cache[index].mutex);

    return;
}

void cache_url_read(int index,char *dst)
{
    /* semaphore */
    P(&cache[index].url_mutex);
    cache[index].url_readcnt++;
    if(cache[index].url_readcnt==1)
        P(&cache[index].url_w);
    V(&cache[index].url_mutex);

    /* begin read operation */
    strcpy(dst,cache[index].url);
    /* end read operation */

    /* semphore */
    P(&cache[index].url_mutex);
    cache[index].url_readcnt--;
    if(cache[index].url_readcnt==0)
        V(&cache[index].url_w);
    V(&cache[index].url_mutex);

    return;
}

int cache_turn_read(int index)
{
    int t;
    /* semaphore */
    P(&cache[index].t_mutex);
    cache[index].t_readcnt++;
    if(cache[index].t_readcnt==1)
        P(&cache[index].t_w);
    V(&cache[index].t_mutex);

    /* begin read operation */
    t=cache[index].turn;
    /* end read operation */

    /* semphore */
    P(&cache[index].t_mutex);
    cache[index].t_readcnt--;
    if(cache[index].t_readcnt==0)
        V(&cache[index].t_w);
    V(&cache[index].t_mutex);

    return t;
}
