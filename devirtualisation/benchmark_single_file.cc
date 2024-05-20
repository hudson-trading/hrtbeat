#include <cstdio>
#include <stdexcept>
#include <random>

class Matrix {
  public:
    int n, m;
    double* arr = nullptr;
    Matrix(int n_, int m_)
    : n(n_)
    , m(m_)
    {
        arr = new double[n * m];
    }

	Matrix(const Matrix& other)
		: Matrix(other.n, other.m) {
		for (int i = 0; i < n; ++i) {
			for (int j = 0; j < m; ++j) {
				arr[i * m + j] = other[i][j];
			}
		}
	}

    virtual ~Matrix() {
        delete[] arr;
        arr = nullptr;
    }

    double* operator[](int subscript) const {
        return arr + subscript * m;
    }

    Matrix& operator=(const Matrix& other) {
        n = other.n;
        m = other.m;
        if (arr != nullptr) {
            delete[] arr;
        }
        arr = new double[n * m];
		for (int i = 0; i < n; ++i) {
			for (int j = 0; j < m; ++j) {
				arr[i * m + j] = other[i][j];
			}
		}
        return *this;
    }

    static Matrix getRandom(int n, int m) {
        std::random_device rd;
        std::mt19937_64 mt(rd());
        std::uniform_real_distribution<> dis(0, 1);
        Matrix result(n, m);
        for (int i = 0; i < n; ++i) {
            double sum = 0;
            for (int j = 0; j < m; ++j) {
                result[i][j] = dis(mt);
                sum += result[i][j];
            }
            for (int j = 0; j < m; ++j) {
                result[i][j] /= sum;
            }
        }
        return result;
    }
};

Matrix operator*(const Matrix& a, const Matrix& b) {
    if (a.m != b.n) {
        throw std::invalid_argument("Attempting to multiply matrices but the sizes don't match!");
    }
    Matrix result(a.n, b.m);
    for (int i = 0; i < a.n; ++i) {
        for (int j = 0; j < b.m; ++j) {
            result[i][j] = 0;
            for (int k = 0; k < a.m; ++k) {
                result[i][j] += a[i][k] * b[k][j];
            }
        }
    }
    return result;
}

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

