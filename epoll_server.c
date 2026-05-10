#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <limits.h>
#include <sys/epoll.h>


typedef struct {
  char method[8];
  char path[256];
  int version;
  char host[256];
  char connection[64];
} http_request_t;

static int should_keep_alive(http_request_t *req){     

  if (req->version == 10){
    return strcasecmp(req->connection, "keep-alive") == 0;
  }
  return strcasecmp(req->connection, "close") != 0;
}

int make_listen_fd(int port){

  int listening_socket_number = socket(AF_INET,SOCK_STREAM | SOCK_NONBLOCK,0);
  if(listening_socket_number < 0){
    perror("make_listen_fd | can't get socket number !");
    return -1;
  }

  int opt = 1;
  int reuseaddr = setsockopt(listening_socket_number,SOL_SOCKET,SO_REUSEADDR,&opt,sizeof(opt));
  if (reuseaddr < 0){
    perror("make_listen_fd | setsockopt Fail's !");
    close(listening_socket_number);
    return -1;
  }

  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  addr.sin_addr.s_addr = htonl(INADDR_ANY);

  int rc = bind (listening_socket_number, (struct sockaddr *)&addr,sizeof(addr));
  if (rc < 0){
    perror("bind");
    close(listening_socket_number);
    return -1;
  }

  int listen_rc = listen(listening_socket_number, 128);
  if (listen_rc < 0){
    perror("make_listen_fd | listen failed !");
    close(listening_socket_number);
    return -1;
  }
  printf("listening fd number: %d\n",listening_socket_number);

  return listening_socket_number;    
}



static int parse_port(const char *s, int *out){
  char *endptr;
  errno = 0;
  long port = strtol(s,&endptr,10);
  if (errno != 0) return -1;
  if (endptr == s) return -1;
  if (*endptr != '\0') return -1;
  if (port < 1 || port > 65535) return -1;
  *out = (int)port;
  return 0;
}

static int parse_http_request(const char *buf, http_request_t *out){
  const char *methods[] = {"GET","POST","PUT","DELETE","OPTIONS"};
  int method_found = -1;
  for (int i = 0;i<5;i++){
    size_t mlen = strlen(methods[i]);
    if (strncmp(buf, methods[i], mlen) == 0 && buf[mlen] == ' '){
      strcpy(out->method,methods[i]);
      method_found = i;
      break;
    }
  }
  if (method_found < 0) return -1;

  const char *path_start = buf + strlen(out->method) + 1;
  const char *path_end = strpbrk(path_start, " \r\n");
  if (path_end == NULL){printf("Bad Path");return -1;}
  size_t path_len = path_end - path_start;
  if(*path_end == ' '){
    const char *v = path_end + 1;
    if (strncmp(v, "HTTP/1.0",8) == 0){out->version = 10;}
    else if (strncmp(v, "HTTP/1.1", 8) == 0){out->version = 11;}
    else return -1;
  }else return -1;
  
  if (path_len >= sizeof(out->path)) return -1;
  memcpy(out->path, path_start, path_len);
  out->path[path_len] = '\0';

  out->host[0] = '\0';
  out->connection[0] = '\0';
  
  const char *line_end = strstr(buf, "\r\n");
  if (line_end == NULL) return -1;
  const char *headers_start = line_end + 2;

  const char *cursor = headers_start;

  while(1){

    if (cursor[0] == '\r' && cursor[1] == '\n') break;
    if (cursor[0] == '\n') break;
    
    const char *eol = strstr(cursor, "\r\n");
    if (eol == NULL) eol = strchr(cursor, '\n');
    if (eol == NULL) break;

    const char *colon = memchr(cursor, ':', eol - cursor);
    if (colon == NULL) {
      cursor = eol + 2;
      continue;
    }

    size_t name_len = colon - cursor;
    const char *value_start = colon + 1;
    while (value_start < eol && (*value_start == ' ' || *value_start == '\t')){
      value_start++;
    }
    size_t value_len = eol - value_start;
    if (name_len == 4 && strncasecmp(cursor, "Host",4) == 0){

      if (value_len < sizeof(out->host)){
        memcpy(out->host, value_start, value_len);
        out->host[value_len] = '\0';
      }
    
    }else if (name_len == 10 && strncasecmp(cursor, "Connection", 10) == 0){
      if (value_len < sizeof(out->connection)){
        memcpy(out->connection, value_start, value_len);
        out->connection[value_len] = '\0';
      }
    }

    cursor = eol + 2;

  }
  return 0;
}

static const char *get_mime_type(const char *path){
  const char *ext = strrchr(path,'.');
  if (ext == NULL) return "application/octet-stream";

  if (strcasecmp(ext,".html") == 0 || strcasecmp(ext,".htm") == 0) return "text/html";
  if (strcasecmp(ext,".txt") == 0) return "text/plain";
  if (strcasecmp(ext,".css") == 0) return "text/css";
  if (strcasecmp(ext,".js") == 0) return "application/javascript";
  if (strcasecmp(ext,".json") == 0) return "application/json";
  if (strcasecmp(ext,".png") == 0) return "image/png";
  if (strcasecmp(ext,".jpeg") == 0 || strcasecmp(ext,".jpg") == 0) return "image/jpeg";

  if (strcasecmp(ext,".gif") == 0) return "image/gif";
  if (strcasecmp(ext,".svg") == 0) return "image/svg+xml";
  if (strcasecmp(ext,".pdf") == 0) return "application/pdf";
  return "application/octet-stream";
}


static int send_response_headers(int client_fd, int version, int status_code,const char *reason,const char *content_type, long content_length, int keep_alive){

  char header_buf[1024];
  const char *conn_str = keep_alive ? "keep-alive" : "close";
  int len = snprintf(header_buf, sizeof(header_buf),
      "HTTP/1.%d %d %s\r\n"
      "Content-Type: %s\r\n"
      "Content-Length: %ld\r\n"
      "Connection: %s\r\n"
      "\r\n",
      version -10,
      status_code,
      reason,
      content_type,
      content_length,
      conn_str);
  if (len < 0 || (size_t)len >= sizeof(header_buf)) return -1; 
  if (send(client_fd, header_buf, len ,0) < 0) return -1;
  

  return 0;
}

static int send_bad_request_response(int client_fd){

  char buf[1024];
  int status = 400;
  const char *reason = "Bad Request";
  const char *body = "<h1>Bad Request</h1>";
  int len = snprintf(buf,sizeof(buf),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: text/html\r\n"
        "Content-Length: %zu\r\n"
        "Connection: close\r\n"
        "\r\n%s",
        status, reason, strlen(body), body);

  if (len < 0 || (size_t)len >= sizeof(buf)) return -1;
  if (send(client_fd, buf, len, 0) < 0) return -1;
  return 0;

}

int main(int argc, char *argv[]) {
  
  signal(SIGPIPE,SIG_IGN);
  
  if (argc != 2){
    fprintf(stderr,"Usage: %s <port>\n",argv[0]);
    exit(1);
  }

  int user_port;
  if (parse_port(argv[1],&user_port) < 0) {
    fprintf(stderr,"invalid port: %s\n",argv[1]);
    exit(1);
  }

  int fd = make_listen_fd(user_port);
  if (fd < 0) { exit(1); }
  int client_fd;

  int epfd = epoll_create1(EPOLL_CLOEXEC);
  if (epfd <0){perror("epoll_create1"); exit(1);}
  
  struct epoll_event ev = {0};
  ev.events = EPOLLIN;
  ev.data.fd = fd;

  if (epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev) < 0){
    perror("epoll_ctl(listen)");
    exit(1);
  }

  struct epoll_event events[64];
  while(1){
    int n = epoll_wait(epfd, events, 64, -1);
    if (n <0) {
      if (errno == EINTR) continue;
      perror("epoll_wait"); 
      break;
    }
    for (int i = 0; i < n; i++){
      if (events[i].data.fd == fd){
        while(1){
          client_fd = accept4(fd, NULL, NULL, SOCK_NONBLOCK | SOCK_CLOEXEC);
          if (client_fd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            perror("accept4"); break;
          }
          fprintf(stderr, "[accept] new fd=%d\n", client_fd);
          close(client_fd);
        }
      }
    }
  }

  return 0;
}


