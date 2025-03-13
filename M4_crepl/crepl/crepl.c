#include <stdio.h>
#include <string.h>
#include <dlfcn.h>
#include <unistd.h>
#include <assert.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>

#define SHM_NAME "/shm"
#define MAX_LEN 100
typedef int (*ExprFunc)();
typedef char Library[MAX_LEN][MAX_LEN];

int main(int argc, char *argv[]) {
    static char line[4096];
    int expr_idx = 0;

    // Create share memory
    int shm_fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666);
    if (shm_fd == -1) {
        perror("shm_open");
        return 1;
    }
    size_t SHM_SIZE = sizeof(int) + sizeof(Library);
    if (ftruncate(shm_fd, SHM_SIZE) == -1) {
        perror("ftruncate");
        return 1;
    }
    void *shm_addr = mmap(NULL, SHM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (shm_addr == MAP_FAILED) {
        perror("mmap");
        return 1;
    }
    int *lk_idx;
    char (*lk_options)[MAX_LEN];
    lk_idx = (int *)shm_addr;
    lk_options = (char (*)[MAX_LEN])((char *)shm_addr + sizeof(int));
    *lk_idx = 0;
    memset(lk_options, 0, sizeof(Library));

    while (1) {
        printf("crepl> ");
        fflush(stdout);

        if (!fgets(line, sizeof(line), stdin)) {
            break;
        }

        const bool is_func = (line[0] == 'i' && line[1] == 'n' && line[2] == 't');
        // Create expression function line
        if (!is_func)
        {
            char line_copy[4096];
            strcpy(line_copy, line);
            char prefix[MAX_LEN];
            sprintf(prefix, "int __expr_wrapper_%d() { return ", expr_idx++);
            char postfix[] = ";}";

            strcpy(line, prefix);
            strcat(line, line_copy);
            strcat(line, postfix);
        }

        // Get function name and .so file path
        char func_name[MAX_LEN];
        char path_so[MAX_LEN];
        memset(func_name, 0, sizeof(func_name));
        memset(path_so, 0, sizeof(path_so));
        char *p_end = strchr(line, '(');
        if (p_end == NULL) 
        {
            fprintf(stderr, "Compile error.\n");
            continue;
        }
        char *p_start = line + 4;
        strncpy(func_name, p_start, p_end - p_start);
        char prefix_so[] = "/tmp/lib";
        char postfix_so[] = ".so";
        strcpy(path_so, prefix_so);
        strcat(path_so, func_name);
        strcat(path_so, postfix_so);

        // 通过另一个进程完成一小段代码到二进制代码的编译
        // 动态库被加载到当前进程的地址空间中
        void *handle = NULL;
        pid_t pid = fork();
        if (pid == 0)
        {
            char path_c[] = "/tmp/XXXXXX.c";
            int fd = mkstemps(path_c, 2);
            if (fd == -1) {
                perror("[mkstemps]");
                continue;
            }
            ssize_t bytes_written = write(fd, line, strlen(line));
            assert(bytes_written != -1);
            close(fd);

            char *args[100];
            int idx = 0;
            args[idx++] = "gcc";
            args[idx++] = "-fPIC";
            args[idx++] = "-shared";
            args[idx++] = "-Wno-implicit-function-declaration";
            args[idx++] = path_c;
            args[idx++] = "-o";
            args[idx++] = path_so;
            args[idx++] = "-L/tmp";
            for (int i = 0; i < *lk_idx; i++)
            {
                args[idx++] = lk_options[i];
            }
            args[idx++] = NULL;

            execvp("gcc", args);
        }
        else
        {
            int status;
            pid_t wpid = waitpid(pid, &status, 0);
            if (wpid == -1) {
                perror("waitpid");
                continue;
            }

            if (WIFEXITED(status)) 
            {
                int exit_status = WEXITSTATUS(status);
                if (exit_status != 0)
                {
                    fprintf(stderr, "Compile error.\n");
                    continue;
                }
                strcpy(lk_options[*lk_idx], "-l");
                strcat(lk_options[*lk_idx], func_name);
                (*lk_idx)++;

                // Load dynamic library
                handle = dlopen(path_so, RTLD_LAZY);
                if (!handle) {
                    fprintf(stderr, "[dlopen]:%s\n", dlerror());
                    continue;
                }
            }
        }

        ExprFunc func;
        char *error;
        // Clear current error
        dlerror();
        func = (ExprFunc)dlsym(handle, func_name);
        if ((error = dlerror()) != NULL) {
            fprintf(stderr, "%s\n", error);
            continue;
        }

        // Call and print expression
        if (!is_func)
        {
            int result = func();
            printf("%d\n", result);
            fflush(stdout);
            // dlclose(handle);
        }
    }

    if (munmap(shm_addr, SHM_SIZE) == -1) {
        perror("munmap");
        return 1;
    }
    if (shm_unlink(SHM_NAME) == -1) {
        perror("shm_unlink");
        return 1;
    }

    return 0;
}
