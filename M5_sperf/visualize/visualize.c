// 仿照less实现管道读入，类似Color样例
// 管道的输入相当于从stdin fd读取?

// pipeb.c的写入参考
#include <stdio.h>
#include <unistd.h>

int main() {
    char buffer[128];
    ssize_t bytesRead;

    // 从标准输入（文件描述符 0）读取数据
    while ((bytesRead = read(STDIN_FILENO, buffer, sizeof(buffer) - 1)) > 0) {
        // 确保字符串以空字符结尾
        buffer[bytesRead] = '\0';
        // 打印读取的数据
        printf("%s", buffer);
    }

    if (bytesRead == -1) {
        perror("read failed");
        return 1;
    }

    return 0;
}