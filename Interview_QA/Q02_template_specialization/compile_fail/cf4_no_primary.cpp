// 期望编译失败：偏特化之前必须先声明主模板（同学答案里漏掉的那一行）
template <typename T> class Traits<T*> {};
int main() {}
