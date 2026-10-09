// 期望编译失败：偏特化不能有默认模板实参
template <typename T, typename U> struct S {};
template <typename T = int> struct S<T, char> {};
int main() {}
