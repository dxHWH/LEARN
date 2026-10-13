// cf8：/GR- 下对多态对象用 typeid
#include <cstdio>
#include <exception>
#include <typeinfo>
struct Base { virtual ~Base() = default; };
struct Derived : Base {};
int main() {
    Derived d;
    Base* b = &d;
    try {
        const char* n = typeid(*b).name();       // /GR- 下：只有 C4541 警告，编译通过
        std::printf("typeid = %s\n", n);
    } catch (const std::exception& e) {
        std::printf("运行期抛出异常: %s\n", e.what());
    }
}
