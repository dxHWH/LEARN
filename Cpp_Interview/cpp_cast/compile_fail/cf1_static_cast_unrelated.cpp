// cf1：static_cast 不能在没有继承关系的类型之间转换（多继承的“侧向转换”只能 dynamic_cast）
struct IRenderable { virtual ~IRenderable() = default; };
struct ITickable   { virtual ~ITickable() = default; };
struct Sprite : IRenderable, ITickable {};
int main() {
    Sprite sp;
    IRenderable* r = &sp;
    ITickable* t = static_cast<ITickable*>(r);   // error：IRenderable 与 ITickable 无关
    float f = 1.0f;
    int* pi = static_cast<int*>(&f);             // error：无关指针类型，static_cast 不做位重解释
    (void)t; (void)pi;
}
