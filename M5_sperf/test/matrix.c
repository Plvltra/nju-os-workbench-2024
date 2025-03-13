#include <stdio.h>
#include <stdlib.h>
#include <time.h>

// 定义矩阵大小
#define MATRIX_SIZE 800
// 定义重复计算的次数
#define ITERATIONS 1

// 初始化矩阵
void initializeMatrix(double** matrix) {
    for (int i = 0; i < MATRIX_SIZE; ++i) {
        for (int j = 0; j < MATRIX_SIZE; ++j) {
            matrix[i][j] = (double)rand() / RAND_MAX;
        }
    }
}

// 矩阵乘法运算
void multiplyMatrices(double** A, double** B, double** C) {
    for (int i = 0; i < MATRIX_SIZE; ++i) {
        for (int j = 0; j < MATRIX_SIZE; ++j) {
            C[i][j] = 0;
            for (int k = 0; k < MATRIX_SIZE; ++k) {
                C[i][j] += A[i][k] * B[k][j];
            }
        }
    }
}

int main() {
    // 使用当前时间作为随机数种子
    srand((unsigned int)time(NULL));

    // 动态分配矩阵 A, B 和 C
    double** A = (double**)malloc(MATRIX_SIZE * sizeof(double*));
    double** B = (double**)malloc(MATRIX_SIZE * sizeof(double*));
    double** C = (double**)malloc(MATRIX_SIZE * sizeof(double*));

    for (int i = 0; i < MATRIX_SIZE; ++i) {
        A[i] = (double*)malloc(MATRIX_SIZE * sizeof(double));
        B[i] = (double*)malloc(MATRIX_SIZE * sizeof(double));
        C[i] = (double*)malloc(MATRIX_SIZE * sizeof(double));
    }

    // 初始化矩阵 A 和 B
    initializeMatrix(A);
    initializeMatrix(B);

    // 记录开始时间
    clock_t start = clock();

    // 执行多次矩阵乘法
    for (int iter = 0; iter < ITERATIONS; ++iter) {
        multiplyMatrices(A, B, C);
    }

    // 记录结束时间
    clock_t end = clock();

    // 计算并输出执行时间
    double elapsed = (double)(end - start) / CLOCKS_PER_SEC;
    printf("Execution time: %.3f seconds\n", elapsed);

    // 输出结果矩阵的一个元素以确保不被优化掉
    printf("C[0][0] = %lf\n", C[0][0]);

    // 释放分配的内存
    for (int i = 0; i < MATRIX_SIZE; ++i) {
        free(A[i]);
        free(B[i]);
        free(C[i]);
    }
    free(A);
    free(B);
    free(C);

    return 0;
}