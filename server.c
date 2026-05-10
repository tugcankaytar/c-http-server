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


typedef struct {
  char method[8];
  char path[256];
  int version;
  char host[256];
  char connection[16];
} http_request_t;

static ssize_t recv_request(int client_fd, char *buf, size_t buf_size){
  size_t total = 0;
  while(total < buf_size -1){
    ssize_t n = recv(client_fd, buf + total, buf_size - 1 - total, 0);
    if (n <0) return -1;
    if (n == 0) {
      if (total == 0) return 0;
      return -1;
    }
    total += n;
    buf[total] = '\0';

    if (strstr(buf, "\r\n\r\n") != NULL) return (ssize_t)total;
  }
  return -1;
}

static int should_keep_alive(http_request_t *req){     
  if (req->version == 9) return 0;

  if (req->version == 10){
    return strcasecmp(req->connection, "keep_alive") == 0;
  }
  return strcasecmp(req->connection, "close") != 0;
}

int make_listen_fd(int port){

  int listening_socket_number = socket(AF_INET,SOCK_STREAM,0);
  if(listening_socket_number < 0){
    perror("make_listen_fd | cant get socket number !");
    return -1;
  }

  int opt = 1;
  int reuseaddr = setsockopt(listening_socket_number,SOL_SOCKET,SO_REUSEADDR,&opt,sizeof(opt));
  if (reuseaddr < 0){
    perror("make_listen_fd | setsockopt Fails !");
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
  printf("listening port: %d\n",listening_socket_number);

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
  }else{
    out->version = 9;
  }
  
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


static int send_response_headers(int client_fd, int version, int status_code,const char *reason,const char *content_type, long content_lenght, int keep_alive){

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
      content_lenght,
      conn_str);
  if (len < 0 || (size_t)len >= sizeof(header_buf)) return -1; 
  if (send(client_fd, header_buf, len ,0) < 0) return -1;
  

  return 0;
}

static int serve_file(int client_fd, http_request_t *req){
  

  if (strstr(req->path, "..")){return -1;}
  
  char full_path[512];
  if (strcmp(req->path,"/") == 0){
    snprintf(full_path, sizeof(full_path),"./www/index.html");
  }else{
    snprintf(full_path, sizeof(full_path), "./www%s",req->path);
  }

  int status_code = 200;
  const char *reason = "OK";
  
  FILE *requested_file = fopen(full_path, "rb");

  if (requested_file == NULL){
    snprintf(full_path, sizeof(full_path),
        "./www/404.html");
    
    requested_file = fopen(full_path,"rb");
    status_code = 404;
    reason = "Not Found";

    if (requested_file == NULL){
      const char *msg = "<h1>Not Found</h1>";
      if (req->version >= 10){
        send_response_headers(client_fd,req->version,404,"Not Found","text/html",strlen(msg),1);
      }
        send(client_fd, msg, strlen(msg), 0);
        return 0;
    }
 }

  char file_buf[4096];
  size_t n;
  
  const char *mime = get_mime_type(full_path);

  fseek(requested_file, 0, SEEK_END);
  long size = ftell(requested_file);
  fseek(requested_file ,0 ,SEEK_SET);
  int keep_alive = should_keep_alive(req);
  
  if (req->version >= 10){send_response_headers(client_fd,req->version,status_code,reason,mime,size,keep_alive);}
  
  while((n = fread(file_buf, 1, sizeof(file_buf),requested_file)) > 0){
      send(client_fd, file_buf, n, 0);
  }
    fclose(requested_file);
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
  while(1){
    
    client_fd = accept(fd, NULL, NULL);
    
    while(1){

      char buf[1024];

      ssize_t n = recv_request(client_fd, buf, sizeof(buf));
      if (n <= 0) break;
      
      buf[n] = '\0';

      http_request_t request;
      parse_http_request(buf, &request);
      serve_file(client_fd, &request);

      if (!should_keep_alive(&request)) break;
    }

    close(client_fd);
  }

  return 0;
}


