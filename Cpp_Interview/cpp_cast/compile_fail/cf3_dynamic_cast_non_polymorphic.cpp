// cf3：dynamic_cast 下行要求源类型是多态类型（至少一个虚函数）
struct Base { int hp = 0; };                     // 没有虚函数 -> 没有 RTTI
struct Derived : Base { int mp = 0; };
int main() {
    Derived d;
    Base* b = &d;
    Derived* p = dynamic_cast<Derived*>(b);      // error
    (void)p;
}
