// 期望编译失败：在隐式实例化之后才声明特化
template <typename T> struct Box { T v; };
Box<int> g_box;                      // 这里已经隐式实例化了 Box<int>
template <> struct Box<int> { long v; };
int main() {}
