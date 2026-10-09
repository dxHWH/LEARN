// C++17 能编译（被当成聚合体，{} 绕过了 =delete），C++20 修复为编译失败
struct NoDefault { NoDefault() = delete; int a; };
int main() { NoDefault* p = new NoDefault{}; (void)p; }
