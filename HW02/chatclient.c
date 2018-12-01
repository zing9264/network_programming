#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <netdb.h>
#include <sys/wait.h>
#include <fcntl.h>
#define BUFSIZE 8096

volatile int isfile = 0;
int fd;
char name[50];
char *IP = "127.0.0.1";
int PORT = 1234;
void init();
void start();
void *task1(); //監聽伺服器廣播
void *task2(); //處理特定文件接收
void send_handler(char *msg);
void recv_file(char *msg);
void fa(int signo)

{
    printf("正在退出...");
    sleep(1);
    close(fd);
    exit(0);
}

void sigchld_handler(int s)
{
  while(waitpid(-1, NULL, WNOHANG) > 0);
}

int main()
{
    struct sigaction sa;
    signal(SIGINT, fa); //将SIGINT設定好 用在退出提示

    sa.sa_handler = sigchld_handler; // 收拾死掉的fork
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    if (sigaction(SIGCHLD, &sa, NULL) == -1) {
        perror("sigaction");
        exit(1);
    }
    //通知內核，自己對子進程的結束不感興趣，那麼子進程結束後，核心會回收， 並不再給父進程 發送信號。

    printf("啟動程式中\n");
    sleep(1);
    printf("請輸入暱稱:");
    scanf("%s", name);
    init(); //與伺服器連線初始化
    write(fd, name, strlen(name));
    start();
    return 0;
}

void init()
{
    /*設定連線socket*/
    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == -1)
    {
        perror("socket");
        exit(-1);
    }

    /*設定addr*/
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    addr.sin_addr.s_addr = inet_addr(IP);
    /*connect*/
    int res = connect(fd, (struct sockaddr *)&addr, sizeof(addr));
    if (res == -1)
    {
        perror("connect");
        exit(-1);
    }
    printf("***連接伺服器成功***\n");
}
void start()
{
    pthread_t pid1, pid2;
    pthread_create(&pid1, 0, task1, 0);
    char msg[5000] = {};
    int i;

    while (1)
    {

        if (isfile == 0)
        {
            fgets(msg, 5000, stdin);
            if (isfile == 1)
            {
                if (fork() == 0)
                {
                    recv_file(msg);
                    close(fd);
                    return;
                }
                wait(NULL);
                isfile = 0;
                memset(msg, 0, strlen(msg));
                
                continue;
            }
            for (i = 0; i < strlen(msg); i++)
            {
                if (msg[i] == '\n')
                {
                    msg[i] = '\0';
                }
            }
            if (strncmp(msg, "!send", 5) == 0)
            {
                if (fork() == 0)
                {
                    send_handler(msg);
                    close(fd);
                    return;
                }
            }
            else
            {
                write(fd, msg, strlen(msg));
            }
        }
        memset(msg, 0, strlen(msg));
    }
}
/*用來讀取伺服器端的訊息*/
void *task1()
{
    while (1)
    {
        if (isfile == 0)
        {

            char buf[5000] = {};
            char from[500] = {};
            char filename[500] = {};
            if (read(fd, buf, sizeof(buf)) <= 0)
                return 0;

            if (strncmp(buf, "****recv****file****",20) == 0)
            {
                sscanf(buf,"****recv****file**** %s %s",from,filename);
                printf("用戶[%s]想傳送檔案[%s]給你,是否接受(y/n):\n", from, filename);
                sleep(1);
                isfile = 1;
                memset(buf, 0, sizeof(buf));
            }
            else
            {
                printf("%s\n", buf);
            }
            memset(buf, 0, sizeof(buf));
        }
    }
}
void send_handler(char *msg)
{
    int file_fd, ret;
    int numbytes;
    char cmd[500] = {};
    char target[50] = {};
    char path[4000] = {};
    char buffer[BUFSIZE];
    FILE *fp;
    memset(cmd, '\0', 500);
    memset(target, '\0', 50);
    memset(path, '\0', 4000);
    sscanf(msg, "%s %s %s", cmd, target, path);

    if(strcmp(target,name)==0){
        printf("你不能發送檔案給自己!\n");
        return;
    }

    fp = fopen(path, "rb");
    if (fp <= 0)
    {
        printf("開檔失敗");
        return;
    }
    //Sendin
    /* 傳回指令 */
    printf("發送檔案中..\n");
    write(fd, cmd, strlen(cmd));
    sleep(1);
    write(fd, target, strlen(target));
    sleep(1);
    write(fd, path, strlen(path));
    sleep(1);

    while (!feof(fp))
    {
        numbytes = fread(buffer, sizeof(char), sizeof(buffer), fp);
        printf("fread %d bytes, ", numbytes);
        numbytes = write(fd, buffer, numbytes);
        printf("Sending %d bytes\n", numbytes);
    }
    sleep(1);
    write(fd, "****SEND****FINISHED****", 24);
    printf("傳送檔案結束,等待對方接收\n");
    close(fd);
    fclose(fp);
    return;
}

void recv_file(char *msg)
{
    char buf[5000] = {};
    char from[500] = {};
    char filename[500] = {};
    int ret, i;
    FILE *write_fp;

    memset(from, 0, sizeof(from));
    memset(filename, 0, sizeof(filename));

    
    read(fd, buf, sizeof(buf));
    sscanf(buf, "%s %s", from, filename);
    if (msg[0] != 'y')
    {
        isfile = 0;
        printf("不同意接受\n");
        return;
    }
    else
    {
        printf("同意接收\n");
        write(fd, "****yes*****", 12);
        sleep(1);
        write(fd, from, sizeof(from));
        write_fp = fopen(filename, "wb");
        //寫入與接收中止條件
        while (1)
        {
            ret = read(fd, buf, sizeof(buf));
            printf("read %d bytes\n", ret);
            if (strstr(buf, "****SEND****FINISHED****") != 0)
            {
                break;
            }
            ret = fwrite(buf, sizeof(char), ret, write_fp);
            printf("fwrite %d bytes\n", ret);
        }
        printf("獲取檔案: %s  完成!\n", filename);
        fclose(write_fp);
    }
    return;
}