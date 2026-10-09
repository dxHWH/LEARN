// 期望编译失败：函数模板不能偏特化
template <typename T> void h(T) {}
template <typename T> void h<T*>(T*) {}
int main() {}
