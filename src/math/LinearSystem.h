#pragma once

#include <vector>
#include <cassert>

struct LinearSystem {
    std::vector<std::vector<double>> A;
    std::vector<double> b;
    int n = 0;

    void resize(int size) {
        n = size;
        A.assign(n, std::vector<double>(n, 0.0));
        b.assign(n, 0.0);
    }

    std::vector<double> solve() {
        std::vector<std::vector<double>> M = A;
        std::vector<double> x = b;

        for (int col = 0; col < n; ++col) {
            int pivot = col;
            for (int row = col + 1; row < n; ++row) {
                if (std::abs(M[row][col]) > std::abs(M[pivot][col]))
                    pivot = row;
            }
            if (std::abs(M[pivot][col]) < 1e-15) continue;
            std::swap(M[col], M[pivot]);
            std::swap(x[col], x[pivot]);

            double piv = M[col][col];
            for (int j = col; j < n; ++j) M[col][j] /= piv;
            x[col] /= piv;

            for (int row = 0; row < n; ++row) {
                if (row == col) continue;
                double f = M[row][col];
                if (std::abs(f) < 1e-15) continue;
                for (int j = col; j < n; ++j) M[row][j] -= f * M[col][j];
                x[row] -= f * x[col];
            }
        }
        return x;
    }
};
