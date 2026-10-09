// delete 不完整类型：编译只给警告，运行时不调用析构函数（未定义行为/资源泄漏）
struct Texture;                               // 只有前向声明
void Release(Texture* t) { delete t; }        // C4150：析构函数不会被调用
int main() { Release(nullptr); }
