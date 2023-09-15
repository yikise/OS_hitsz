#include "kernel/types.h"
#include "user.h"

int main(int argc,char* argv[]){
    int p1[2];
    int p2[2];
    char buff1[4] = "ping";
    char buff2[4] = "pong";
    pipe(p1);
    pipe(p2);
    int pid = fork();

    if(pid > 0) {
        //父线程
        //往第一个管道里写ping
        close(p1[0]);
        write(p1[1], buff1, sizeof buff1);
        close(p1[1]);
        //等待子进程结束
        wait((int *) 0);
        close(p1[1]);
        //从第二个管道中读pong
        read(p2[0], buff2, sizeof buff2);
        printf("%d: received %s\n", getpid(), buff2);
        close(p2[0]);
    } else if(pid == 0) {
        //子线程
        //从第一个管道里读ping
        close(p1[1]);
        read(p1[0], buff1, sizeof buff1);
        close(p1[0]);
        printf("%d: received %s\n", getpid(), buff1);
        //往第二个管道里写pong
        close(p2[0]);
        write(p2[1], buff2, sizeof buff2);
        close(p2[1]);
        exit(0);
    }
    exit(0); //确保进程退出
}
