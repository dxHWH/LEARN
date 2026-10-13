// cf4：只有 const_cast 能去掉 const；const_cast 也只能改 cv，不能改类型
int main() {
    const int ci = 1;
    int* a = static_cast<int*>(&ci);             // error：static_cast 不能去 const
    const float cf = 1.0f;
    int* b = reinterpret_cast<int*>(&cf);        // error：reinterpret_cast 也不能去 const
    int x = 0;
    double* c = const_cast<double*>(&x);         // error：const_cast 不能改变类型
    (void)a; (void)b; (void)c;
}
