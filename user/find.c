#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"

//该函数用于输出最后一个文件名
char* fmtname(char *path)
{
  char *p;
  // Find first character after last slash.
  for(p=path+strlen(path); p >= path && *p != '/'; p--)
    ;
  p++;
  return p;
}

void find(char *path, char *file_name)
{
  char buf[512], *p;
  int fd;
  struct dirent de;
  struct stat st;

  if((fd = open(path, 0)) < 0){
    fprintf(2, "ls: cannot open %s\n", path);
    return;
  }

  if(fstat(fd, &st) < 0){
    fprintf(2, "ls: cannot stat %s\n", path);
    close(fd);
    return;
  }

  switch(st.type){
  case T_FILE:
  //当st的type就是文件时：
    //当文件名就是需要寻找的时候，直接输出
    if(strcmp(fmtname(path), file_name) == 0) printf("%s\n", path);
    break;

  case T_DIR:
  //当st的type是文件夹时：
    if(strlen(path) + 1 + DIRSIZ + 1 > sizeof buf){
      printf("ls: path too long\n");
      break;
    }
    strcpy(buf, path);
    p = buf+strlen(buf);
    *p++ = '/';
    //读取文件夹下的每一个文件
    while(read(fd, &de, sizeof(de)) == sizeof(de)){
      if(de.inum == 0)
        continue;
      memmove(p, de.name, DIRSIZ);
      p[DIRSIZ] = 0;
      if(stat(buf, &st) < 0){
        printf("ls: cannot stat %s\n", buf);
        continue;
      }
      //递归find，但不递归进入.和..
      if(strcmp(fmtname(buf), ".") != 0 && strcmp(fmtname(buf), "..") != 0) {
        find(buf, file_name);
      }
    }
    break;
  }
  close(fd);
}

int main(int argc, char *argv[])
{
  //当参数低于三个时，直接退出
  if(argc < 3){
    exit(0);
  }
  //输入进方法find
  find(argv[1], argv[2]);
  exit(0);
}
