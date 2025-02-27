// Martin Kersner, m.kersner@gmail.com
// 2017/04/21

#include <string.h>
#include <map>
#include "gtop.hh"

std::mutex m;
std::condition_variable cv;
tegrastats t_stats;

bool processed = false;
bool ready     = false;
bool finished  = false;

static jetson_version tegra_chip_id;

static const std::map<std::string, int> TK1cpuIdxMap = {};
static const std::map<std::string, int> TK1gpuIdxMap = {};
static const std::map<std::string, int> TX1cpuIdxMap = { std::make_pair ("28-1.0", 5) };
static const std::map<std::string, int> TX1gpuIdxMap = { std::make_pair ("28-1.0", 7) };
static const std::map<std::string, int> TX2cpuIdxMap = { std::make_pair ("28-2.1", 5) };
static const std::map<std::string, int> TX2gpuIdxMap = { std::make_pair ("28-2.1", 9) };

static int cpuStatsIdx;
static int gpuStatsIdx;



int main() {
  if (getuid()) {
    std::cout << "gtop requires root privileges!" << std::endl;
    exit(1);
  }

  /* Identify the jetson we're running on */
  char jetsonIdStr[3];
  FILE* fid = fopen ("/sys/module/tegra_fuse/parameters/tegra_chip_id", "r");
  if (!fid) {
          std::cerr << "Error: failed to open /sys/module/tegra_fuse/parameters/tegra_chip_id" << std::endl;
  }
  int readBytes = fread(jetsonIdStr, 1, 2, fid);
  if (readBytes != 2) {
	  std::cerr << "Error: failed to read /sys/module/tegra_fuse/parameters/tegra_chip_id" << std::endl;
  }
  fclose(fid);
  jetsonIdStr[2]=0;
  if (!strcmp(jetsonIdStr, "24"))
	tegra_chip_id = TX2;
  else if (!strcmp(jetsonIdStr, "33"))
	tegra_chip_id = TX1;
  else if (!strcmp(jetsonIdStr, "64"))
	tegra_chip_id = TK1;
  else {
	std::cerr << "Error: unsupported Jetson with code " << jetsonIdStr << std::endl;
	exit (-2);
  }


  /* Get L4T version */
  char l4tStr[32];
  fid = fopen ("/etc/nv_tegra_release", "r");
  if (!fid) {
	  std::cerr << "Error: failed to open /etc/nv_tegra_release" << std::endl;
  }
  readBytes = fread(l4tStr, 1, 32, fid);
  if (readBytes != 32) {
	  std::cerr << "Error: failed to read /etc/nv_tegra_release" << std::endl;
  }
  fclose(fid);

  char l4t_version[8];
  l4t_version[0] = l4tStr[3];
  l4t_version[1] = l4tStr[4];
  l4t_version[2] = '-';
  l4t_version[3] = l4tStr[27];
  l4t_version[4] = l4tStr[28];
  l4t_version[5] = l4tStr[29];
  l4t_version[6] = 0;
  printf("Found L4T version %s\n", l4t_version);
 
  std::map<std::string, int>::const_iterator it;
  switch (tegra_chip_id) {
	  case TX2:
		it = TX2cpuIdxMap.find(l4t_version);
		if (it == TX2cpuIdxMap.end()) {
			std::cerr << "Error: CPU index undefined for L4T version " << l4t_version <<std::endl;
			exit(-2);
		}
		cpuStatsIdx = (*it).second;
		
		it = TX2gpuIdxMap.find(l4t_version);
		if (it == TX2gpuIdxMap.end()) {
			std::cerr << "Error: GPU index undefined for L4T version " << l4t_version <<std::endl;
			exit(-3);
		}
		gpuStatsIdx = (*it).second;
		break;
		
	  case TX1:
		it = TX1cpuIdxMap.find(l4t_version);
		if (it == TX1cpuIdxMap.end()) {
			std::cerr << "Error: CPU index undefined for L4T version " << l4t_version <<std::endl;
			exit(-2);
		}
		cpuStatsIdx = (*it).second;
		
		it = TX1gpuIdxMap.find(l4t_version);
		if (it == TX1gpuIdxMap.end()) {
			std::cerr << "Error: GPU index undefined for L4T version " << l4t_version <<std::endl;
			exit(-3);
		}
		gpuStatsIdx = (*it).second;
		break;
		
	  case TK1:
		it = TK1cpuIdxMap.find(l4t_version);
		if (it == TK1cpuIdxMap.end()) {
			std::cerr << "Error: CPU index undefined for L4T version " << l4t_version <<std::endl;
			exit(-2);
		}
		cpuStatsIdx = (*it).second;
		
		it = TK1gpuIdxMap.find(l4t_version);
		if (it == TK1gpuIdxMap.end()) {
			std::cerr << "Error: GPU index undefined for L4T version " << l4t_version <<std::endl;
			exit(-3);
		}
		gpuStatsIdx = (*it).second;
		break;
  }

 

  std::thread t(read_tegrastats); 

  initscr();
  noecho();
  timeout(1);

  start_color();
  init_pair(WHITE_BLACK,   COLOR_WHITE,   COLOR_BLACK);
  init_pair(RED_BLACK,     COLOR_RED,     COLOR_BLACK);
  init_pair(GREEN_BLACK,   COLOR_GREEN,   COLOR_BLACK);
  init_pair(YELLOW_BLACK,  COLOR_YELLOW,  COLOR_BLACK);
  init_pair(BLUE_BLACK,    COLOR_BLUE,    COLOR_BLACK);
  init_pair(MAGENTA_BLACK, COLOR_MAGENTA, COLOR_BLACK);
  init_pair(CYAN_BLACK,    COLOR_CYAN,    COLOR_BLACK);

  std::vector<std::vector<int>> cpu_usage_buffer;

  while (1) {
    {
      std::lock_guard<std::mutex> lk(m);
      ready = true;
    }
    cv.notify_one();

    std::unique_lock<std::mutex> lk(m);
    cv.wait(lk, []{ return processed; });
    processed = false;

    // CPU, GPU, MEM STATS
    dimensions dim_stats;
    display_stats(dim_stats, t_stats);

    // CPU USAGE CHART
    update_usage_chart(cpu_usage_buffer, t_stats.cpu_usage);
    display_usage_chart(10, cpu_usage_buffer);


    lk.unlock();

    refresh();

    if (getch() == 'q') // press 'q' or Ctrl-C to quit
      break;
  }

  { 
    std::lock_guard<std::mutex> lk(m);
    finished = true;
  }

  t.join();
  endwin();

  return 0;
}

void read_tegrastats() {
  std::array<char, STATS_BUFFER_SIZE> buffer;

#ifdef TEGRASTATS_FAKE
  std::shared_ptr<FILE> pipe(popen(TEGRASTATSFAKE_PATH.c_str(), "r"), pclose);
#else
  if (!file_exists(TEGRASTATS_PATH)) {
    std::cerr << "tegrastats was not found at the expected location "
              << TEGRASTATS_PATH << std::endl;
    exit(1); // TODO terminate correctly
  }

  std::shared_ptr<FILE> pipe(popen(TEGRASTATS_PATH.c_str(), "r"), pclose);
#endif

  if (!pipe)
    throw std::runtime_error ("popen() failed!");

  while (!feof(pipe.get())) {

    if (fgets(buffer.data(), STATS_BUFFER_SIZE, pipe.get()) != NULL) {
      std::unique_lock<std::mutex> lk(m);

      // terminate reading tegrastats
      if (finished) {
        lk.unlock();
        break;
      }

      cv.wait(lk, []{ return ready; });
      ready = false;
      t_stats = parse_tegrastats(buffer.data());
      processed = true;
      lk.unlock();
      cv.notify_one();
    }
  }
}

tegrastats parse_tegrastats(const char * buffer) {
  tegrastats ts;
  ts.version = tegra_chip_id;
  
  auto stats = tokenize(buffer, ' ');

  get_mem_stats(ts, stats.at(1));
  
  int swap_offset = 0;
  if (stats.at(4) == "SWAP") {
	  swap_offset = 4;
  }

  switch (ts.version) {
    case TX1:
      get_cpu_stats_tx1(ts, stats.at(cpuStatsIdx + swap_offset));
      get_gpu_stats(ts, stats.at(gpuStatsIdx + swap_offset));
      break;
    case TX2:
      get_cpu_stats_tx2(ts, stats.at(cpuStatsIdx + swap_offset));
      get_gpu_stats(ts, stats.at(gpuStatsIdx + swap_offset));
      break;
    case TK1: // TODO
      break;
  }

  return ts;
}

void get_cpu_stats_tx1(tegrastats & ts, const std::string & str) {
  auto cpu_stats = tokenize(str, '@');
  auto cpu_usage_all = cpu_stats.at(0);
  ts.cpu_freq.push_back(std::stoi(cpu_stats.at(1)));
  auto cpu_usage = tokenize(cpu_usage_all.substr(1, cpu_usage_all.size()-2), ',');

  for (const auto & u: cpu_usage) {
    if (u != "off")
      ts.cpu_usage.push_back(std::stoi(u.substr(0, u.size()-1)));
    else
      ts.cpu_usage.push_back(0);
  }
}

void get_cpu_stats_tx2(tegrastats & ts, const std::string & str) {
  const auto cpu_stats = tokenize(str.substr(1, str.size()-1), ',');
  const auto at = std::string("@");

  for (const auto & u: cpu_stats) {
    if (u != "off") {
      std::size_t found = at.find(u);
      if (found == std::string::npos) {
        auto cpu_info = tokenize(u, at.c_str()[0]);
        ts.cpu_usage.push_back(std::stoi(cpu_info.at(0).substr(0, cpu_info.at(0).size()-1)));
        ts.cpu_freq.push_back(std::stoi(cpu_info.at(1)));
      }
    }
    else {
      ts.cpu_usage.push_back(0);
      ts.cpu_freq.push_back(0);
    }
  }
}

void get_gpu_stats(tegrastats & ts, const std::string & str) {
  const auto gpu_stats = tokenize(str, '@');
  const auto gpu_usage = gpu_stats.at(0);

  ts.gpu_usage = std::stoi(gpu_usage.substr(0, gpu_usage.size()-1));
  ts.gpu_freq = std::stoi(gpu_stats.at(1));
}

void get_mem_stats(tegrastats & ts, const std::string & str) {
  const auto mem_stats = tokenize(str, '/');
  const auto mem_max = mem_stats.at(1);

  ts.mem_usage = std::stoi(mem_stats.at(0));
  ts.mem_max = std::stoi(mem_max.substr(0, mem_max.size()-2));
}

void display_stats(const dimensions & d, const tegrastats & ts) {
  // CPU
  display_cpu_stats(0, ts);

  // GPU
  display_gpu_stats(ts.cpu_usage.size(), ts);

  // Memory
  display_mem_stats(ts.cpu_usage.size()+1, ts);
}

void update_usage_chart(std::vector<std::vector<int>> & usage_buffer,
                        const std::vector<int> & usage)
{
  widget w = update_widget_dims(0);

  if (usage_buffer.size() >= w.max_x)
    usage_buffer.erase(usage_buffer.begin());

  usage_buffer.push_back(usage);
}
