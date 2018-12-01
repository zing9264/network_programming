/*
請實作一個multi-thread版的群組聊天室server，
也就是server會建立一個新的thread來服務每個client。
client在剛連上server時會送出自己的帳號名稱(這裡不做密碼認證)，
之後client可以
(1) 查看有哪些使用者正在server線上
(2) 送出訊息給所有連到同一server的client
(3) 定傳送訊息給正在線上的某個使用者
(4) 也可以傳送檔案給另一個使用者，對方可決定是否接收
作業demo時須至少展示有三個使用者上線。
*/

#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <pthread.h>
#include <arpa/inet.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

char *IP = "127.0.0.1";//伺服器端IP
int PORT = 1234;
int fd;

typedef struct clients//連線中用戶資料
{
    char name[20];
    int socket;
    char sendfile[500];
} Client;

Client client[100]; //能同時存放100個連線中的用戶
int size = 0;       //當前連線數量

void fa(int signo)
{
    printf("伺服器出現問題\n");
    sleep(1);
    close(fd);
    printf("伺服器已關機\n");
    exit(0);
}
/*thread function*/
void *task(void *p);
void *task2(void *p);

/*發送訊息函數*/
void sendmsgtoALL(char *msg);
void sendmsgtoTARGET(char *msg,int sock);
void sendfiletoTARGET(char *from,char *file,char *filename,char *name);

void listMember(int sock);
void init();
void start();
void closeServe();

int main()
{
    signal(SIGINT, fa);//設定SIGINT為退出訊息
    init();//初始化
    start();
    closeServe();
}
void init()
{
    printf("***伺服器初始化中***\n");
    /*set socket*/
    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == -1){
        perror("socket"); 
        exit(-1);
    }
    /*設定addr*/
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    addr.sin_addr.s_addr = inet_addr(IP);
    /*bind*/
    int res = bind(fd, (struct sockaddr *)&addr, sizeof(addr));
    if (res == -1){
        perror("bind");
        exit(-1);
    }
       
    /*listen*/
    listen(fd, 100);//上限100人
    printf("***伺服器初始化完成！***\n");
}
void start()
{
    printf("***伺服器開啟成功!***\n");
    printf("***等待用戶端連結...***\n");
    while (1)
    {
        /*創建用戶端地址訊息*/
        struct sockaddr_in from;
        socklen_t len = sizeof(from);
        int sockfd = accept(fd, (struct sockaddr *)&from, &len);
        if (sockfd == -1){
            perror("accept");
            exit(-1);
        }
        /*accept成功後創立 thread*/
        client[size].socket = sockfd;
        pthread_t p_id;
        pthread_create(&p_id, 0, task, (void *)&(client[size].socket));
    }
}
void closeServe()
{
    printf("***伺服器正在關閉***\n");
    sleep(1);
    close(fd);
    printf("***伺服器關閉成功!***\n");
    exit(0);
}

void *task(void *sock)
{
    int socktemp = *((int *)sock);
    char name[50] = {}; //用戶名稱
    char buf[5000] = {};
    char help[5000]= {"-----輸入以下指令-----\n!help:\t開啟說明指令\n!check:\t查詢連線中的使用者名稱\n!send username ./file(path):\t發送檔案\n@username message:\t向特定用戶發送私人訊息\n"};
    int i, temp;
    if (read(client[size].socket, name, sizeof(name)) > 0)
        strcpy(client[size].name, name);
    temp = size;
    size++;
    /*第一次連接時印出訊息*/
    char remind[5000] = {};
    /*密語用字串*/
    int flag=0;
    char targetname[50]={};
    char wsp[5000]= {};
    char mywsp[5000]= {};
    /*檔案用變數*/
    FILE *write_fp;
    int ret;
    char filename[4000];
    char tmp[] = "temp-XXXXXX";
    int tmpfd;

    sprintf(remind, "***伺服器公告：歡迎%s加入聊天***\n", client[size - 1].name);
    printf("***伺服器公告：歡迎%s加入聊天***\n", client[size - 1].name);
    sendmsgtoALL(remind);
    sendmsgtoTARGET(help,socktemp);
    while (1)
    {
        if (read(socktemp, buf, sizeof(buf)) <= 0)
        {
            char remind1[5000] = {};
            sprintf(remind1, "***伺服器公告：%s退出聊天***\n", client[temp].name);
            printf("***伺服器公告：%s退出聊天***\n", client[temp].name);
            sendmsgtoALL(remind1);
            for (i = 0; i < size; i++)
                if (client[i].socket == socktemp)
                {
                    client[i].socket = 0;
                }
            return 0; //結束thread
        }
        //處理訊息
        if(buf[0]=='!'){
               if(strncmp(buf+1,"check",5)==0){
                   printf("用戶:[%s]請求人員列表\n",client[temp].name);
                   listMember(socktemp);
               }
                if(strncmp(buf+1,"help",4)==0){
                   printf("用戶:[%s]請求指令提示\n",client[temp].name);
                    sendmsgtoTARGET(help,socktemp);
               }
                if(strncmp(buf+1,"send",4)==0){
                    //讀取命令與分割字串
                    read(socktemp, targetname, sizeof(targetname));
                    read(socktemp, filename, sizeof(filename));
                    printf("接收到send,送出對象為%s,文件名%s\n",targetname,filename);        
                    
                    //建立暫存資料夾
                    printf("建立暫存檔中...\n");
                    sleep(1);
                    tmpfd = mkstemp(tmp);
                    close(tmpfd);
                    printf("暫存資料檔:%s 生成\n",tmp);
                   
                    write_fp=fopen(tmp,"wb");
                    //寫入暫存資料夾與接收中止條件
                    while(1){
                        ret = read(socktemp, buf, sizeof(buf));
                        printf("read %d bytes\n", ret);
                        if(strcmp(buf,"****SEND****FINISHED****")== 0){
                            break;
                        }
                        ret = fwrite(buf, sizeof(char), ret, write_fp);
                        printf("fwrite %d bytes\n", ret);
                    }
                    printf("獲取檔案: %s 從 client[%s] 完成!\n", filename, client[temp].name);  
                    strcpy(client[temp].sendfile,tmp);
                    fclose(write_fp);
                    sendfiletoTARGET(client[temp].name,tmp,filename,targetname);
                }
            memset(buf, 0, strlen(buf));
        }
        else if(buf[0]=='@'){
            //字串擷取
            int j=0;
           for(j=1;j<50;j++){
               if(buf[j]==' '){
                   targetname[j-1]='\0';
                   break;
               }
               targetname[j-1]=buf[j];
            }
            sprintf(wsp,"來自[%s]:%s", client[temp].name,buf+j+1 );
            sprintf(mywsp,"發送給[%s]:%s", targetname,buf+j+1 );
            for(i=0;i<size;i++){
                if(strcmp(client[i].name,targetname)==0){
                    flag=1;
                    break;
                }
            }
            if(flag==0){
                sendmsgtoTARGET("該用戶不存在\n",socktemp);
            }
            else{
                sendmsgtoTARGET(mywsp,socktemp);
                sendmsgtoTARGET(wsp,client[i].socket);
                memset(buf, 0, strlen(buf));
            }

        }
        else if(strcmp(buf,"****yes*****")==0){
                printf("用戶同意接收\n");
                FILE *fp;
                char getname[50]={};
                char readpath[500]={};
                read(socktemp, getname, sizeof(getname));

                for(i=0;i<size;i++){
                    if(strcmp(client[i].name,getname)==0){
                    strcpy(readpath,client[i].sendfile);
                    break;
                    }
	            }

                int numbytes;
                fp=fopen(readpath,"rb");
                
                //Sending file
                memset(buf, 0, strlen(buf));
                while(!feof(fp)){
                        numbytes = fread(buf, sizeof(char), sizeof(buf), fp);
                        printf("fread %d bytes, ", numbytes);
                        numbytes = write(socktemp, buf, numbytes);
                        printf("Sending %d bytes\n",numbytes);
                    }
                sleep(1);
                write(socktemp,"****SEND****FINISHED****", 24);
                printf("傳送檔案結束\n");
                fclose(fp);
        }
        else{
        /*向所有人發送訊息*/
            char msg[5000] = {};

            if(strlen(buf)<=0){
                memset(buf, 0, strlen(buf));
                continue;
            }
                
            sprintf(msg, "[%s]:%s", client[temp].name, buf);
            sendmsgtoALL(msg);
            memset(buf, 0, strlen(buf));
        }
    }
}

void sendmsgtoALL(char * msg){
    int i=0;	
    for(i=0;i<size;i++){
		if(client[i].socket!=0){
			printf("***發送廣播訊息給%s***\n",client[i].name);
			send(client[i].socket,msg,strlen(msg),0);
        }
	}
}
void sendmsgtoTARGET(char *msg,int sock){
    send(sock,msg,strlen(msg),0);
}

void listMember(int sock){
    int i=0;
    char memberlist[5000];
    memset(memberlist,'\0',5000);
    for(i=0;i<size;i++){
		if(client[i].socket!=0){
			sprintf(memberlist+strlen(memberlist),"%s\n",client[i].name);
        }
	}
    sendmsgtoTARGET("------人員列表------\n",sock);
    sendmsgtoTARGET(memberlist,sock);
}

void sendfiletoTARGET(char *from,char *file,char *filename,char *name){

    int i=0;
    int targetsock,fromsock;
    int flag=0;
    char msg[5000]={};
    char buf[5000]={};
    printf("尋找用戶中\n");
    for(i=0;i<size;i++){
		if(strcmp(client[i].name,from)==0){
		fromsock=client[i].socket;
        break;
        }
	}
    for(i=0;i<size;i++){
		if(strcmp(client[i].name,name)==0){
		targetsock=client[i].socket;
        flag=1;
        break;
        }
	}
    if(flag==0){
        printf("找不到用戶[%s]\n",name);
        sprintf(msg,"找不到用戶[%s]\n",name);
        sendmsgtoTARGET(msg,fromsock);
        return;
    }
    printf("發送訊息中...\n");

    sprintf(msg,"****recv****file**** %s %s",from,filename);
    sendmsgtoTARGET(msg,targetsock);
    memset(msg, '\0', strlen(msg));
    sleep(1);
    sprintf(msg,"%s %s",from,filename);
    sendmsgtoTARGET(msg,targetsock);
    memset(msg, '\0', strlen(msg));
    return;
}
