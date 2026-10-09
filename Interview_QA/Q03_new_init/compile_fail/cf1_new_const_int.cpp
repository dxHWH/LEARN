// 期望编译失败：const 对象必须被初始化，default-init 的 const int 什么都没做
int main() { const int* p = new const int; (void)p; }
