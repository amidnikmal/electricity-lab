#pragma once

#include <vector>
#include <cmath>
#include <string>
#include <limits>
#include <cassert>

struct LinearSolveResult {
    std::vector<double> x;
    bool singular = false;
    int singularColumn = -1;
    bool ok = false;
    int rank = 0;
    double residual = 0.0;
    double rcond = 0.0;
    std::string status;
};

struct LinearSystem {
    std::vector<std::vector<double>> A;
    std::vector<double> b;
    int n = 0;

    void resize(int size) {
        n = size;
        A.assign(n, std::vector<double>(n, 0.0));
        b.assign(n, 0.0);
    }

    LinearSolveResult solve() {
        LinearSolveResult result;
        std::vector<std::vector<double>> M = A;
        result.x = b;

        double maxPivot = 0.0;
        double minPivot = std::numeric_limits<double>::max();

        for (int col = 0; col < n; ++col) {
            int pivot = col;
            for (int row = col + 1; row < n; ++row) {
                if (std::abs(M[row][col]) > std::abs(M[pivot][col]))
                    pivot = row;
            }
            double pivAbs = std::abs(M[pivot][col]);
            if (pivAbs < 1e-15) {
                result.singular = true;
                result.singularColumn = col;
                continue;
            }
            if (pivAbs > maxPivot) maxPivot = pivAbs;
            if (pivAbs < minPivot) minPivot = pivAbs;

            std::swap(M[col], M[pivot]);
            std::swap(result.x[col], result.x[pivot]);

            double piv = M[col][col];
            for (int j = col; j < n; ++j) M[col][j] /= piv;
            result.x[col] /= piv;

            for (int row = 0; row < n; ++row) {
                if (row == col) continue;
                double f = M[row][col];
                if (std::abs(f) < 1e-15) continue;
                for (int j = col; j < n; ++j) M[row][j] -= f * M[col][j];
                result.x[row] -= f * result.x[col];
            }
        }

        result.rank = n;
        if (result.singular)
            result.rank = result.singularColumn;

        double normRes = 0.0, normB = 0.0;
        for (int i = 0; i < n; ++i) {
            double sum = 0.0;
            for (int j = 0; j < n; ++j) sum += A[i][j] * result.x[j];
            double r = sum - b[i];
            normRes += r * r;
            normB += b[i] * b[i];
        }
        result.residual = std::sqrt(normRes) / (std::sqrt(normB) + 1e-15);

        result.rcond = (maxPivot > 1e-15) ? (minPivot / maxPivot) : 0.0;

        if (result.singular) {
            result.status = "singular";
            result.ok = false;
        } else if (result.rcond < 1e-14) {
            result.status = "ill-conditioned";
            result.ok = true;
        } else if (result.residual > 1e-6) {
            result.status = "inconsistent";
            result.ok = false;
        } else {
            result.status = "ok";
            result.ok = true;
        }

        return result;
    }
};
