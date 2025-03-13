#include <stdio.h>
#include <unistd.h>

int main() {
    int i = 0;
    while (1) {
        printf("Increment number: %d\n", i++);
    }
    return 0;
}
