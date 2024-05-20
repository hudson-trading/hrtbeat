#include <cstdio>

#include "benchmark_miltiple_files.h"

int main() {
    constexpr int N = 16;
    constexpr int L = 1000 * 1000 * 10;
    auto a = Matrix::getRandom(N, N);
    Matrix res = a;
    for (int i = 0; i < L; ++i) {
        res = res * a;
    }
    for (int i = 0; i < N; ++i) {
        for (int j = 0; j < N; ++j) {
            printf("%.4lf ", res[i][j]);
        }
        printf("\n");
    }
}

