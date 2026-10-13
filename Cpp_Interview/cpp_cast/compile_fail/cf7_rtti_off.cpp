// cf7：用 /GR- 关闭 RTTI 后仍然写 dynamic_cast（很多游戏工程关闭 RTTI）
#include <cstdio>
#include <exception>
struct Base { virtual ~Base() = default; };
struct Derived : Base {};
int main() {
    Derived d;
    Base* b = &d;
    try {
        Derived* p = dynamic_cast<Derived*>(b);  // /GR- 下：只有 C4541 警告，编译通过
        std::printf("结果 = %p\n", (void*)p);
    } catch (const std::exception& e) {
        std::printf("运行期抛出异常: %s\n", e.what());
    }
}
