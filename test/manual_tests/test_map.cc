#include <map>
#include <chrono>
#include <string>
#include <iostream>

void gen_map(std::map<std::string, std::string>* bm, int num, int time) {
  for (int i = 0; i < num; i++) {
    std::string key = std::to_string(rand() % num + num * time);
    std::string value = std::string(20, 'a' + (i % 26));
    (*bm)[key] = value;
  }
}

void gen_ordered_map(std::map<std::string, std::string>* bm, int num, int time) {
  for (int i = 0; i < num; i++) {
    std::string key = std::to_string(num);
    std::string value = std::string(20, 'a' + (i % 26));
    (*bm)[key] = value;
  }
}

void test_insert(int num, int loop) {
  srand(1);
  std::map<std::string, std::string> base;
  gen_map(&base, num, 0);
  for (int i = 0; i < loop; i++) {
    std::map<std::string, std::string> n;
    gen_map(&n, num, i+1);
    base.merge(n);
  }
  std::cout << "Size: " << base.size() << std::endl;
}

void test_update(int num, int loop) {
  srand(1);
  std::map<std::string, std::string> base;
  gen_map(&base, num, 0);
  for (int i = 0; i < loop; i++) {
    std::map<std::string, std::string> n;
    gen_map(&n, num, 0);
    base.merge(n);
  }
  std::cout << "Size: " << base.size() << std::endl;
}

void test_ordered_insert(int num, int loop) {
  std::map<std::string, std::string> base;
  gen_ordered_map(&base, num, 0);
  for (int i = 0; i < loop; i++) {
    std::map<std::string, std::string> n;
    gen_ordered_map(&n, num, 0);
    base.merge(n);
  }
}

// Seems insert is more expensive than update

int main() {
  int map_size = 128;
  int loop = 100000;
  auto start = std::chrono::steady_clock::now();
  test_insert(map_size, loop);
  auto end = std::chrono::steady_clock::now();
  std::chrono::duration<double> elapsed_seconds = end-start;
  std::cout << "Test insert: " << elapsed_seconds.count() << "s\n";

  start = std::chrono::steady_clock::now();
  test_update(map_size, loop);
  end = std::chrono::steady_clock::now();
  elapsed_seconds = end-start;
  std::cout << "Test update: " << elapsed_seconds.count() << "s\n";

  start = std::chrono::steady_clock::now();
  test_ordered_insert(map_size, loop);
  end = std::chrono::steady_clock::now();
  elapsed_seconds = end-start;
  std::cout << "Test ordered insert: " << elapsed_seconds.count() << "s\n";

// Test insert: 16.0555s
// Test update: 9.30133s
// Test ordered insert: 1.8223s
}