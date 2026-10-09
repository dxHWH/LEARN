// 类内声明了 operator new(size_t)，会隐藏全局的 placement new 和 nothrow new
#include <new>
#include <cstdlib>
struct Tracer {
    static void* operator new(std::size_t sz) { return std::malloc(sz); }
    static void operator delete(void* p) noexcept { std::free(p); }
};
int main() {
    alignas(Tracer) unsigned char buf[sizeof(Tracer)];
    Tracer* a = new (buf) Tracer;            // 错误：类作用域里找不到 operator new(size_t, void*)
    Tracer* b = new (std::nothrow) Tracer;   // 错误：同理找不到 nothrow 版本
    Tracer* c = ::new (buf) Tracer;          // OK：:: 强制全局查找
    (void)a; (void)b; (void)c;
}
