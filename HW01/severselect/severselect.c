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
#include <sys/time.h>


#define PORT "80" 
#define BACKLOG 10 // pending connections queue
#define BUFSIZE 12500



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

int select_handle(int fd)
{
    int j, file_fd, buflen, len;
    long i;
    char * fstr;
    char *buffer;
    long ret;
    
    printf("reading\n");
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

    return 0;
}


int main(void)
{

    fd_set master; // 主幹 file descriptor 
    fd_set tmp_fds; // 給 select() 用的暫時 file descriptor 清單
    int fdmax; // 最大的 file descriptor 數目

    int listenfd, new_fd; // 在 listenfd 進行 listen，new_fd 是新的連線
    int rv;
    struct addrinfo hints, *servinfo, *p;
    struct sockaddr_storage their_addr; // 連線者的位址資訊 
    socklen_t sin_size;
    int yes=1,i;
    long nbyte=0; //檢查回傳byte;
    char buf[BUFSIZE+1];

    FD_ZERO(&master); // 清除 master 與 temp sets
    FD_ZERO(&tmp_fds);

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM; //tcp
    hints.ai_flags = AI_PASSIVE; // 使用我的 IP
    hints.ai_protocol=0;// any

    if((rv=getaddrinfo(NULL, PORT, &hints, &servinfo))!=0){
          fprintf(stderr, "selectserver: %s\n", gai_strerror(rv));
         exit(1);
    };//利用getaddrinfo初始化並且儲存資訊

    for(p=servinfo;p!=NULL;p=p->ai_next){
        listenfd = socket(p->ai_family, p->ai_socktype,p->ai_protocol);
        if(listenfd<0){
            continue;
        }

        setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &yes,sizeof(int));
        
        if(bind(listenfd, p->ai_addr, p->ai_addrlen)==-1){ //use port 80 need superuser
            close(listenfd);
            continue;
        }
        break;
    }

    if(p==NULL){
        fprintf(stderr, "selectserver: failed to bind\n");
        exit(2);
    }

    freeaddrinfo(servinfo); // 釋放 servinfo 記憶體空間

    listen(listenfd, BACKLOG);


    printf("server: waiting for connection\n");
    FD_SET(listenfd, &master);
    if(fdmax < listenfd){
        fdmax = listenfd;
    }
    printf("\n%d,%d\n",fdmax,listenfd);
    while(1) { // 主要的 accept() 迴圈

        tmp_fds = master;

        if (select(fdmax+1, &tmp_fds, NULL, NULL, NULL) == -1) {
        perror("select");
        exit(4);
        }


        for(i = listenfd; i <= fdmax; i++) {  // 在現存的連線中尋找需要讀取的資料
            if (FD_ISSET(i, &tmp_fds)) { // 找到一個
                printf("i=%d,listen=%d\n",i,listenfd);
                    if (i == listenfd) {//是sever的socket處理連線
                        // handle new connections
                        sin_size = sizeof their_addr;
                        printf("----accept----\n");
                        new_fd = accept(listenfd, (struct sockaddr *)&their_addr, &sin_size);
                        if (new_fd == -1) {
                            perror("accept");
                            continue;
                        }
                        else {
                        FD_SET(new_fd, &master); // 新增到 master set
                            if (new_fd > fdmax) { // 持續追蹤最大的 fd
                                fdmax = new_fd;
                                printf("new Max fd is %d",fdmax);
                            }
                        printf("selectserver: new connection on socket %d\n",new_fd);
                        }
                    }
                    else{
                        // 處理來自 client 的資料
                        printf("recving");
                        printf("select_handle\n");
                        select_handle(i);
                        close(i);
                        printf("selectserver: socket %d hung up\n", i);
                        FD_CLR(i, &master); // 從 master set 中移除
                    }
            }  
        }
    }
    return 0;
}