#include <chrono>
#include <cstdio>
#include <random>
#include <vector>
#include <algorithm>
int main(){
  const int n=20000; std::mt19937 r(42); std::vector<int> src(n); for(auto&x:src) x=r()&0x7fffffff;
  auto t=[&](auto f,const char*nm){ auto v=src; auto t0=std::chrono::steady_clock::now(); f(v.data(),n); double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-t0).count(); printf("%s %.2f ms sorted=%d\n",nm,ms,(int)std::is_sorted(v.begin(),v.end())); };
  t([](int*a,int n){ for(int i=0;i<n-1;++i){int m=i; for(int j=i+1;j<n;++j) if(a[j]<a[m]) m=j; std::swap(a[i],a[m]);} },"sel_index");
  t([](int*a,int n){ for(int i=0;i<n-1;++i){int m=i; int mv=a[i]; for(int j=i+1;j<n;++j) if(a[j]<mv){mv=a[j]; m=j;} std::swap(a[i],a[m]);} },"sel_cache");
}
