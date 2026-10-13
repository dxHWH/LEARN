// cf2：不能用 static_cast 从虚基类下行（虚基类偏移只能运行期查）
struct VBase { virtual ~VBase() = default; };
struct VDerived : virtual VBase {};
int main() {
    VDerived vd;
    VBase* vb = &vd;
    VDerived* d = static_cast<VDerived*>(vb);    // error
    (void)d;
}
