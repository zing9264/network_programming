#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>
#include <fcntl.h>
#define PORT "80" // 提供給使用者連線的 port
#define BACKLOG 10 // 有多少個特定的連線佇列（pending connections queue）
#define BUFSIZE 8096



struct {
    char *ext;
    char *filetype;
} FileType [] = {
    {"gif", "image/gif" },
    {"jpg", "image/jpeg"},
    {"jpeg","image/jpeg"},
    {"png", "image/png" },
    {"zip", "image/zip" },
    {"gz",  "image/gz"  },
    {"tar", "image/tar" },
    {"htm", "text/html" },
    {"html","text/html" },
    {"exe","text/plain" },
    {0,0} };

void fork_handle(int fd)
{
    int j, file_fd, buflen, len;
    long i, ret;
    char * fstr;
    static char buffer[BUFSIZE+1];

    ret = read(fd,buffer,BUFSIZE);   // 讀取瀏覽器要求 
    printf("%s\n",buffer);

    if (ret==0||ret==-1) { //連線有問題
        exit(3);
    }

    // 方便後續程式判斷結尾 
    if (ret>0&&ret<BUFSIZE)
        buffer[ret] = 0;
    else
        buffer[0] = 0;

    // 移除換行字元 
    for (i=0;i<ret;i++) 
        if (buffer[i]=='\r'||buffer[i]=='\n'){
            buffer[i] = 0;
        }
    
    // 只接受 GET 
    if (strncmp(buffer,"GET ",4)!=0)
        exit(3);
    
    
    /* 用空字元當空白隔開 */
    for(i=4;i<BUFSIZE;i++) {
        if(buffer[i] == ' ') {
            buffer[i] = 0;
            break;
        }
    }

    // 預設根目錄讀取 index.html 
    if (!strncmp(&buffer[0],"GET /\0",6)){
        strcpy(buffer,"GET /index.html\0");
    }

    //檢查要求的檔案格式 
    buflen = strlen(buffer);
    fstr = (char *)0;

    for(i=0;FileType[i].ext!=0;i++) {
        len = strlen(FileType[i].ext);
        if(!strncmp(&buffer[buflen-len], FileType[i].ext, len)) {
            fstr = FileType[i].filetype;
            break;
        }
    }

    /* 檔案格式不支援 */
    if(fstr == 0) {
        fstr = FileType[i-1].filetype;
    }

    /* 開啟檔案 */
    if((file_fd=open(&buffer[5],O_RDONLY))==-1)
    write(fd, "Failed to open file", 19);

    /* 傳回瀏覽器成功碼 200 和格式 */
    sprintf(buffer,"HTTP/1.0 200 OK\r\nContent-Type: %s\r\n\r\n", fstr);
    write(fd,buffer,strlen(buffer));

    /* 讀取檔案內容輸出到客戶端瀏覽器 */
    while ((ret=read(file_fd, buffer, BUFSIZE))>0) {
        write(fd,buffer,ret);
    }

    exit(1);
}

void sigchld_handler(int s)
{
  while(waitpid(-1, NULL, WNOHANG) > 0);
}

int main(void)
{
    int listenfd, new_fd; // 在 listenfd 進行 listen，new_fd 是新的連線
    struct addrinfo hints, *servinfo, *p;
    struct sockaddr_storage their_addr; // 連線者的位址資訊 
    socklen_t sin_size;
    struct sigaction sa;
    int yes=1;


    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM; //tcp
    hints.ai_flags = AI_PASSIVE; // 使用我的 IP
    hints.ai_protocol=0;// any

    getaddrinfo(NULL, PORT, &hints, &servinfo);//利用getaddrinfo初始化並且儲存資訊

    listenfd = socket(servinfo->ai_family, servinfo->ai_socktype,servinfo->ai_protocol);

    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &yes,sizeof(int));

    if(bind(listenfd, servinfo->ai_addr, servinfo->ai_addrlen)==-1){ //use port 80 need superuser
        perror("bind");
        exit(1);
    }

    freeaddrinfo(servinfo); // 釋放 servinfo 記憶體空間

    listen(listenfd, BACKLOG);


    sa.sa_handler = sigchld_handler; // 收拾死掉的fork
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    if (sigaction(SIGCHLD, &sa, NULL) == -1) {
        perror("sigaction");
        exit(1);
    }

    printf("server: waiting for connection\n");

    while(1) { // 主要的 accept() 迴圈
        sin_size = sizeof their_addr;
        new_fd = accept(listenfd, (struct sockaddr *)&their_addr, &sin_size);
        if (new_fd == -1) {
        perror("accept");
        continue;
        }
    
        if (fork()== 0) { // child process
        close(listenfd); // 不需要 listen
        fork_handle(new_fd);
        close(new_fd);
        exit(0);
        }
        close(new_fd); // parent 不需要
    }
    return 0;
    }