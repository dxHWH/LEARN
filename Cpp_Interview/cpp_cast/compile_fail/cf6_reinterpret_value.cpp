// cf6：reinterpret_cast 只做“指针/引用/整数”之间的位重解释，不能做数值转换
int main() {
    int i = 0x3F800000;
    float f = reinterpret_cast<float>(i);        // error：想要位重解释请用 std::bit_cast
    double d = 3.5;
    int n = reinterpret_cast<int>(d);            // error：数值转换请用 static_cast
    (void)f; (void)n;
}
