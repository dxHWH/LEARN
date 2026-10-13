// cf5：static_cast 遵守访问控制，不能转到 private 基类（C 风格转换却可以，见 demo 第 9 节）
struct Unit { virtual ~Unit() = default; };
struct Secret : private Unit {};
int main() {
    Secret s;
    Unit* u = static_cast<Unit*>(&s);            // error
    (void)u;
}
