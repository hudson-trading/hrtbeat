class Matrix {
  public:
    int n, m;
    double* arr = nullptr;
    Matrix(int n_, int m_);
    Matrix(const Matrix& other);
    virtual ~Matrix();
    double* operator[](int subscript) const;
    Matrix& operator=(const Matrix& other);
    static Matrix getRandom(int n, int m);
};

Matrix operator*(const Matrix& a, const Matrix& b);

