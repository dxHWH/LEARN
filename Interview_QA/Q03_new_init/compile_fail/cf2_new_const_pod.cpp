// 期望编译失败：没有用户提供的默认构造函数的 const 类对象，也不能 default-init
struct Pod { int a; };
int main() { const Pod* p = new const Pod; (void)p; }
