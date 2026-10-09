// 期望编译失败：局部变量写 Pod p(); 是“函数声明”，不是值初始化（most vexing parse）
struct Pod { int a; };
int main() { Pod p(); p.a = 1; }
