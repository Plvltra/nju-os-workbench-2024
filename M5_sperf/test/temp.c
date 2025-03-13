#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <sys/ptrace.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/user.h>
#include <sys/reg.h>
#include <unistd.h>
#include <string.h>
#include <regex.h>
#include <fcntl.h>

int main(int argc, char *argv[], char *envp[]) {
    int fd = open("test.txt", O_WRONLY | O_CREAT | O_TRUNC, 0644);

    // // 使用文件描述符进行低级别的读写操作
    // const char *message = "Hello, World!";
    // write(fd, message, strlen(message));

    // // 重置文件偏移到文件开头
    // lseek(fd, 0, SEEK_SET);

    // char buffer[20];
    // read(fd, buffer, strlen(message));
    // buffer[strlen(message)] = '\0';  // 添加字符串结尾
    // printf("%s\n", buffer);

    // // 关闭文件描述符
    // close(fd);

    // // 选择性地删除文件
    // unlink(template);

    int pipefd[2];
    if (pipe(pipefd) == -1)
    {
        perror("[pipe]");
        exit(EXIT_FAILURE);
    }

    pid_t pid = fork();
    if (pid == 0)
    {
        close(pipefd[0]); // close read

        close(STDOUT_FILENO);
        close(STDERR_FILENO);

        // if (dup2(pipefd[1], fd) == -1)
        // {
        //     perror("[dup2]");
        //     exit(EXIT_FAILURE);
        // }

        char *strace_path = "/usr/bin/strace";
        char *strace_argv[100];
        const int FIXED_LENGTH = 3;
        strace_argv[0] = "strace";
        strace_argv[1] = "-T";
        strace_argv[2] = "-o";
        char fd_path[30];
        sprintf(fd_path, "/proc/%d/fd/%d", getpid(), fd);
        // sprintf(fd_path, "/proc/%d/fd/%d", getpid(), pipefd[1]);
        strace_argv[3] = fd_path;
        for (int i = 1; i <= argc; i++) // NULL include
        {
            strace_argv[FIXED_LENGTH + i] = argv[i];
        }
        execve(strace_path, strace_argv, envp);
    }
    else
    {
        close(pipefd[1]); // close write
        char buffer[65536];
        pid_t status = 0;
        while (status == 0)
        {
            usleep(100000); // delay 100ms

            memset(buffer, 0, sizeof(buffer));
            int read_count = read(pipefd[0], buffer, sizeof(buffer));
            printf("%s", buffer);

            status = waitpid(pid, &status, WNOHANG);
            if (status == -1)
            {
                perror("waitpid failed");
                exit(EXIT_FAILURE);
            }
        }
    }

    return 0;
}
