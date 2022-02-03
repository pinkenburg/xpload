#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <iterator>
#include <numeric>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "xpload/xpload.h"


/**
 * Splits a [0, b] interval into segments of integer length by n distinct random
 * points.
 */
std::vector<int> split_interval(int b, int n)
{
  bool prereq = (b > 0 && n > 0 && n <= b + 1);
  if (!prereq)
  {
    std::cerr << "Assertion failed: (b > 0 && n > 0 && n <= b + 1)\n";
    return {};
  }

  // Generate a vector of indices [0, ..., b]
  std::vector<int> indices(b+1);
  std::iota(indices.begin(), indices.end(), 0);

  // Pick n distinct random indices
  std::vector<int> isegments;
  for (int i=0; i<n; ++i)
  {
    int rand_index = std::rand() % indices.size(); // % is biased but ok for the purpose
    isegments.push_back(indices[rand_index]);
    indices.erase(indices.begin() + rand_index);
  }

  std::sort(isegments.begin(), isegments.end());

  std::vector<int> segments;
  std::adjacent_difference(isegments.begin(), isegments.end(), std::back_inserter(segments));

  // Ensure that the entire [0, b] interval is covered by segments by adding
  // the last point b unless it is already selected
  if (b != isegments.back())
    segments.push_back(b - isegments.back());

  return segments;
}

struct Tokens {
  uint64_t timestamp;
  std::string tag, domain, payload;
};

auto random_tokens(std::pair<int, int> tag_range, std::pair<int, int> dom_range, std::pair<int, int> tst_range)
{
  if (tag_range.first > tag_range.second ||
      dom_range.first > dom_range.second ||
      tst_range.first > tst_range.second)
  {
    std::cerr << "Assertion failed: a <= b in [a, b]\n";
    return Tokens{};
  }

  uint64_t timestamp  = tst_range.first + std::rand() % (tst_range.second - tst_range.first + 1);
  int tag_index       = tag_range.first + std::rand() % (tag_range.second - tag_range.first + 1);
  int dom_index       = dom_range.first + std::rand() % (dom_range.second - dom_range.first + 1);

  std::ostringstream tag; tag << "Tag_" << tag_index;
  std::ostringstream domain; domain << "Domain_" << dom_index;
  std::ostringstream payload; payload << "Payload_" << timestamp << "_Commit_" << tag_index << "_Domain_" << dom_index;

  return Tokens{timestamp, tag.str(), domain.str(), payload.str()};
}


/**
 * Usage:
 *
 * $ test_xpload_rand <b> <n> [rand_seed] [rand_once]
 *
 * <b> is a positive integer defining a closed interval [0, b]
 * <n> is a number of calls to be made within the interval
 * [rand_seed] is a seed for the random number generator
 * [rand_once] is a flag to generate tag/domain/timestamp tokens only once
 */
int main(int argc, char *argv[])
{
  using namespace std;

  // Process command line options
  vector<string> args;
  for (int i=1; i < argc; ++i)
    args.push_back( string(argv[i]) );

  int b = (args.size() > 0) ? stoi(args[0]) : 100;
  int n = (args.size() > 1) ? stoi(args[1]) : ceil(b/10.);
  int rand_seed = (args.size() > 2) ? stoi(args[2]) : 12345;
  int rand_once = (args.size() > 3 && stoi(args[3]) != 0) ? true : false;

  vector<int> segments = split_interval(b, n);

  int sum = accumulate(segments.begin(), segments.end(), 0);
  if (sum != b) {
    cerr << "Assertion failed: (sum == b)\n";
    return EXIT_FAILURE;
  }

  std::srand(rand_seed);

  string cfg = getenv("XPLOAD_CONFIG_NAME") ? string(getenv("XPLOAD_CONFIG_NAME")) : "test";
  xpload::Configurator config(cfg);

  // Print the header
  if (config.db.verbosity > 0)
    cout << "time, duration, wait, byte_count, response_code, path, error_code\n";

  Tokens tk;
  bool generate_tokens = true;

  for (int segment : segments)
  {
    this_thread::sleep_for(chrono::seconds(segment));

    if (generate_tokens) {
      tk = random_tokens({17, 19}, {5, 10}, {300, 301});
      if (rand_once) generate_tokens = false;
    }

    auto t0 = chrono::system_clock::now();
    auto t1 = chrono::high_resolution_clock::now();
    xpload::Result result = xpload::fetch(tk.tag, tk.domain, tk.timestamp, config);
    auto t2 = chrono::high_resolution_clock::now();

    chrono::duration<double, std::milli> td = t2 - t1;

    int error_code = 0;

    if (result.paths.size() != 1)
    {
      cerr << "Expected single payload but got " << result.paths.size() << "\n";
      error_code = 1;
    } else if ( result.paths[0] != config.db.path + "/" + tk.payload) {
      cerr << "Expected " << tk.payload << " but got " << result.paths[0] << "\n";
      error_code = 2;
    }

    if (config.db.verbosity > 1)
      cout << "OK in " << td.count() << " ms after " << segment << " s " << result.byte_count << " B \"" << result.paths[0] << "\"\n";
    else if (config.db.verbosity > 0)
      cout << chrono::system_clock::to_time_t(t0) << ", " << td.count() << ", " << segment << ", "
           << result.byte_count << ", "
           << result.response_code << ", \""
           << (!error_code ? result.paths[0] : "") << "\", "
           << error_code << "\n";
  }

  return EXIT_SUCCESS;
}
