// 期望编译失败：{} 禁止窄化转换，() 允许
int main() { int* a = new int(3.5); int* b = new int{3.5}; (void)a; (void)b; }
