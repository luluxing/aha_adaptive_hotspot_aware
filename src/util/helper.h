#include <fstream>


namespace WOT_NAMESPACE {

std::string Trim(const std::string &str) {
  auto front = std::find_if_not(str.begin(), str.end(), [](int c){ return std::isspace(c); });
  return std::string(front, std::find_if_not(str.rbegin(), std::string::const_reverse_iterator(front),
      [](int c){ return std::isspace(c); }).base());
}

void LoadOptions(const std::string& filename,
                    std::map<std::string, std::string>* options) {
  std::ifstream input(filename);
  if (!input.is_open()) {
    return;
  }
  while (!input.eof() && !input.bad()) {
    std::string line;
    std::getline(input, line);
    if (line[0] == '#')
      continue;
    size_t pos = line.find_first_of('=');
    if (pos == std::string::npos) continue;
    (*options)[Trim(line.substr(0, pos))] = Trim(line.substr(pos + 1));
  }
}

inline uint64_t _rdtsc(){
  uint32_t lo, hi;
  asm volatile("rdtsc" : "=a" (lo), "=d" (hi));
  return (((uint64_t)hi << 32) | lo);
}

} // namespace WOT_NAMESPACE
