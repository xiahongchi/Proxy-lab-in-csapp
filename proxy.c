#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

typedef struct cache_elem{
    char uri[100];
    char *data;
    unsigned size;
    unsigned read;
    struct cache_elem *next;
}cache_elem; 
cache_elem *head;
unsigned readcnt;
sem_t r,w;

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";
void parse_request(int fd,char* server_host,char* server_port,
                char* uri_parsed,char* headers);
void read_requesthdrs(rio_t *rp,char* headers);
void clienterror(int fd, char *cause, char *errnum, 
		 char *shortmsg, char *longmsg);
void parse_uri(char* uri,char* server_host,char* server_port,
                char* uri_parsed);
void read_server_ret(rio_t *rio,char* server_ret);
void* connfd_handler(void* p_connfd);
void receive_return(int clientfd,int connfd,char *uri_parsed);
void insert(cache_elem *tmp);
int find_cache(char *uri,int connfd);


int main(int argc, char **argv)
{
    int listenfd, connfd;
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;

    /* Check command line args */
    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }

    Sem_init(&r,0,1);
    Sem_init(&w,0,1);

    listenfd = Open_listenfd(argv[1]);

    while (1) {

        //first step: receive from browser
        clientlen = sizeof(clientaddr);
        connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
        
        int *p_connfd=(int *)Malloc(sizeof(connfd));
        *p_connfd=connfd;
        pthread_t tid;
        Pthread_create(&tid,NULL,connfd_handler,(void*)p_connfd);

    }
    return 0;
}

void parse_request(int fd,char* server_host,char* server_port,
                char* uri_parsed,char* headers) 
{  
    char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
    
    rio_t rio;

    /* Read request line and headers */
    Rio_readinitb(&rio, fd);
    if (!Rio_readlineb(&rio, buf, MAXLINE))  return;
    sscanf(buf, "%s %s %s", method, uri, version);   
    parse_uri(uri,server_host,server_port,uri_parsed);
    read_requesthdrs(&rio,headers);   

}

void parse_uri(char* uri,char* server_host,char* server_port,char* uri_parsed){
    char* uri_p=uri+7;
    char* p1=strchr(uri_p,':');
    char* p2=strchr(uri_p,'/');
    if(p1&&p2&&p2>p1){
        *p1='\0';*p2='\0';
        strcpy(server_host,uri_p);
        strcpy(server_port,p1+1);
        *p2='/';
        strcpy(uri_parsed,p2);
    }
    else if(p1&&!p2){
        *p1='\0';
        strcpy(server_host,uri_p);
        strcpy(server_port,p1+1);
        strcpy(uri_parsed,"/");
    }
    else if((!p1&&p2)||(p1&&p2)){
        *p2='\0';
        strcpy(server_host,uri_p);
        strcpy(server_port,"80");
        *p2='/';
        strcpy(uri_parsed,p2);
    }
    else{
        strcpy(server_host,uri_p);
        strcpy(server_port,"80");
        strcpy(uri_parsed,"/");
    }
    return;
}

void read_requesthdrs(rio_t *rp,char* headers) 
{
    char buf[MAXLINE];
    char *p;
    unsigned host=0,usa=0,con=0,pcon=0;
    strcpy(headers,"");
    while(1){
        Rio_readlineb(rp, buf, MAXLINE);
        p=strchr(buf,':');
        if(p){
            *p='\0';
            if(strcasecmp(buf,"Host")==0){
                host=1;
            }
            else if(strcasecmp(buf,"User-Agent")==0){
                usa=1;
                strcpy(p+2,user_agent_hdr);
            }
            else if(strcasecmp(buf,"Connection")==0){
                con=1;
                strcpy(p+1," close\r\n");
            }
            else if(strcasecmp(buf,"Proxy-Connection")==0){
                pcon=1;
                strcpy(p+1," close\r\n");
            }
            *p=':';
        }
        if(strcmp(buf,"\r\n")==0){
            if(usa==0){
                sprintf(headers,"%sUser-Agent: %s",headers,user_agent_hdr);
            }
            if(con==0){
                sprintf(headers,"%sConnection: close\r\n",headers);
            }
            if(pcon==0){
                sprintf(headers,"%sProxy-Connection: close\r\n",headers);
            }
            strcat(headers,buf);
            break;
        }
        strcat(headers,buf);
    }
    return;
}

void read_server_ret(rio_t *rio,char* server_ret){
    char buf[MAXLINE];
    strcpy(server_ret,"");
    while(Rio_readlineb(rio,buf,MAXLINE)!=0){
        strcat(server_ret,buf);
    }
}

void* connfd_handler(void* p_connfd){

    int clientfd;
    char server_host[MAXLINE], server_port[MAXLINE];
    char uri_parsed[MAXLINE],headers[MAXLINE];
    char requestline[MAXLINE];
    int connfd=*(int*)p_connfd;
    Free(p_connfd);
    
    Pthread_detach(Pthread_self());

    //second step: send to the server
    parse_request(connfd,server_host,server_port,uri_parsed,headers);
    
    if(find_cache(uri_parsed,connfd)){
        Close(connfd);
        return NULL;
    }

    sprintf(requestline,"GET %s HTTP/1.0\r\n",uri_parsed);
    clientfd=Open_clientfd(server_host,server_port);
    
    Rio_writen(clientfd,requestline,strlen(requestline));
    Rio_writen(clientfd,headers,strlen(headers));

    //third step: receive and return
    receive_return(clientfd,connfd,uri_parsed);

    Close(connfd);

    return NULL;
}

void receive_return(int clientfd,int connfd,char *uri_parsed){
    char buf[MAXLINE],header[MAXLINE];
    char *server_ret;
    char*p,*q;
    unsigned filesize,headsize;
    rio_t rio;

    Rio_readinitb(&rio,clientfd);
    headsize=0;
    strcpy(header,"");
    do{
        Rio_readlineb(&rio,buf,MAXLINE);
        strcat(header,buf);
        if(strncasecmp(buf,"Content-length:",15)==0){
            p=buf+16;
            q=strchr(p,'\r');
            *q='\0';
            filesize=atoi(p);
            *q='\r';
        }
        headsize+=strlen(buf);
        Rio_writen(connfd,buf,strlen(buf));
    }while(strcmp(buf,"\r\n"));
    server_ret=(char*)Malloc(filesize);
    Rio_readnb(&rio,server_ret,filesize);
    
    if(headsize+filesize<=MAX_OBJECT_SIZE){
        cache_elem *tmp=(cache_elem*)Malloc(sizeof(cache_elem));
        tmp->size=headsize+filesize;
        tmp->data=(char*)Malloc(headsize+filesize);
        memcpy(tmp->data,header,headsize);
        memcpy(tmp->data+headsize,server_ret,filesize);
        strcpy(tmp->uri,uri_parsed);
        tmp->next=NULL;
        insert(tmp);
    }

    Close(clientfd);
    
    Rio_writen(connfd,server_ret,filesize);
    Free(server_ret);
}

void insert(cache_elem *tmp){
    P(&w);

    //write happens here!
    cache_elem *p,*q;
    unsigned total_size=0;

    tmp->next=head;
    head=tmp;

    p=head;
    while(p){
        total_size+=p->size;
        if(total_size>MAX_CACHE_SIZE){
            while(p){
                p=p->next;
                Free((q->next)->data);
                Free(q->next);
            }
            break;
        }
        if(p==head){
            q=head;
            p=p->next;
        }
        else{
            p=p->next;
            q=q->next;
        }
    }

    V(&w);
}

int find_cache(char *uri,int connfd){

    cache_elem *p=head,*q;

    P(&r);
    readcnt++;
    if(readcnt==1){
        P(&w);
        //let read bits equals zero
        while(p){
            p->read=0;
            p=p->next;
        }
    }
    V(&r);

    //read happens here!
    p=head;
    while(p){  
        if(strcmp(uri,p->uri)==0){
            p->read=1;
            Rio_writen(connfd,p->data,p->size);
            break;
        }
        p=p->next;
    }

    P(&r);
    readcnt--;
    if(readcnt==0){
        //if read bits equals one, turn it to the front
        p=head;
        while(p){
            if(p->read==1&&p!=head){
                q->next=p->next;
                p->next=head;
                head=p;
                p=q->next;
                continue;
            }
            if(p==head){
                q=head;
                p=p->next;
            }
            else{
                p=p->next;
                q=q->next;
            }
        }
        V(&w);
    }
    V(&r);

    return (p)? 1:0;
}
