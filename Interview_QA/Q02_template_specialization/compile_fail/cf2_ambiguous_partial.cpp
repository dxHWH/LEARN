// 期望编译失败：两个偏特化同样特殊 -> 歧义
template <typename K, typename V> struct Pair {};
template <typename K> struct Pair<K, int> {};
template <typename T> struct Pair<T, T> {};
int main() { Pair<int, int> p; (void)p; }
