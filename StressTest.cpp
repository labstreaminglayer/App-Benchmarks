#include <atomic>
#include <ctime>
#include <iostream>
#include <lsl_cpp.h>
#include <random>
#include <thread>
#include <vector>

// key stress constants
const int max_outlets = 15;
const int max_inlets = 20;
const int min_chunk_len_ms = 1;
const int max_chunk_len_ms = 100;
const int max_inlet_poll_interval_ms = 100;
const int outlet_max_failure_interval_ms = 2000;
const int inlet_min_failure_interval_ms = 1;
const int max_outlet_duration = 10;
const double spawn_inlet_interval = 0.5;
const double spawn_outlet_interval = 0.5;
const int max_srate = 1000;
const int max_channels = 10;
const int max_buffered = 6;

// misc parameters
const int max_chunk_oversize_factor = 5;
const int max_samples = 10000000;

std::atomic<int> num_outlets(0);
std::atomic<int> num_inlets(0);

// some random names, types and formats
const std::vector<lsl::channel_format_t> fmts{
	lsl::cf_int8, lsl::cf_int16, lsl::cf_int32, lsl::cf_float32, lsl::cf_double64, lsl::cf_string};
const std::vector<std::string> names{"Test1", "Test2", "Test3", "Test4"};
const std::vector<std::string> types{"EEG", "Audio", "MoCap"};

// set to true by the main program when we're ready to quit
bool stop_inlet = false, stop_outlet = false, start_outlet = false;

void sleep(double seconds) {
	std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int>(seconds * 1000)));
}

// initialize a sample with data
template <class T> void init_sample(int numchan, std::vector<T>& sample) {
	sample.resize(numchan);
	std::fill(sample.begin(), sample.end(), static_cast<T>(17.3));
}

// run an outlet for some time (optionally with sporadic interruptions in between)
void run_outlet(const double duration_ = 0.0, const std::string &name_ = "",
	const std::string &type_ = "", const int numchan_ = 0,
	const lsl::channel_format_t fmt_ = lsl::cf_undefined, const double srate_ = 0.0,
	const double seconds_between_failures_ = 0.0, const int chunk_len_ = 0) {
	num_outlets++;
	// srand(std::this_thread::get_id()));
	try {
		// choose random parameters if desired
		double duration = (duration_ == 0.0) ? 1.0 + rand() % (max_outlet_duration - 1) : duration_;
		std::string name = name_.empty() ? names[rand() % names.size()] : name_;
		std::string type = type_.empty() ? types[rand() % types.size()] : type_;
		int numchan = (numchan_ == 0) ? 1 + (rand() % (max_channels - 1)) : numchan_;
		double srate = (srate_ == 0.0) ? 1.0 + (rand() % (max_srate - 1)) : srate_;
		lsl::channel_format_t fmt = (fmt_ == lsl::cf_undefined) ? fmts[rand() % 6] : fmt_;
		double seconds_between_failures =
		    (seconds_between_failures_ == 0.0)
		        ? (inlet_min_failure_interval_ms + rand() % outlet_max_failure_interval_ms) / 1000.0
		        : seconds_between_failures_;
		int chunk_len = (chunk_len_ == 0) ? std::max(min_chunk_len_ms, (rand() % max_chunk_len_ms))
		                                  : chunk_len_;

		// create a new streaminfo
		lsl::stream_info info(name, type, numchan, srate, fmt, std::to_string(rand()));

		// initialize data to send
		std::vector<float> chunk(
			(int)(numchan * floor(chunk_len * srate / 1000 * max_chunk_oversize_factor)), 17.3);

		// and run...
		for (double endtime = lsl::local_clock() + duration; lsl::local_clock() < endtime;) {
			// run a single execution of the outlet
			std::cout << "new outlet(" << name << "," << type << "," << numchan << "," << fmt << ","
					  << srate << ")...";
			lsl::stream_outlet outlet(info, 0, max_buffered);
			std::cout << "done." << std::endl;
			// send in bursts
			double now, start_time = lsl::local_clock(),
			            fail_at = start_time + seconds_between_failures;
			for (int target, diff, written = 0; written < max_samples && !stop_outlet;
			     written += diff) {
				std::this_thread::sleep_for(std::chrono::milliseconds(chunk_len));
				now = lsl::local_clock();
				if (now > fail_at) break;
				target = (int)floor((now - start_time) * srate);
				int num_elements =
				    (int)std::min((std::size_t)((target - written) * numchan), chunk.size());
				if (num_elements) outlet.push_chunk_multiplexed(&chunk[0], num_elements);
				diff = num_elements / numchan;
			}
			std::cout << "del outlet(" << name << "," << type << "," << numchan << "," << fmt << ","
					  << srate << ")" << std::endl;
			// downtime
			std::this_thread::sleep_for(std::chrono::milliseconds(100 * (rand() % 50)));
		}
	} catch (std::exception& e) {
		std::cerr << "ERROR during run_outlet() Stress-test function: " << e.what() << std::endl;
	}
	num_outlets--;
}

// run an inlet for some time (optionally with sporadic interruptions in between)
void run_inlet(const double duration_ = 0.0, const std::string& name_ = "", const std::string& type_ = "",
               const int /*in_chunks_*/ = 0, const int request_info_ = -1,
               const int request_time_ = -1, const double seconds_between_failures_ = 0.0) {
	num_inlets.fetch_add(1);
	// srand(...);
	try {
		// choose random parameters if desired
		double duration = (duration_ == 0.0) ? 1.0 + rand() % (max_outlet_duration - 1) : duration_;
		std::string name = name_.empty() ? names[rand() % names.size()] : name_;
		std::string type = type_.empty() ? types[rand() % types.size()] : type_;
		int request_info = (request_info_ == -1) ? rand() % 3 == 0 : request_info_;
		int request_time = (request_time_ == -1) ? rand() % 3 == 0 : request_time_;
		double seconds_between_failures =
		    (seconds_between_failures_ == 0.0)
		        ? (inlet_min_failure_interval_ms + rand() % outlet_max_failure_interval_ms) / 1000.0
		        : seconds_between_failures_;

		// resolve by type...
		std::vector<lsl::stream_info> results = lsl::resolve_stream("type", type, 1, 5);
		if (results.empty()) throw lsl::lost_error("No stream found.");
		lsl::stream_info result = results[rand() % results.size()];
		std::vector<float> chunk;

		// and run...
		double t = 0.0;
		for (double endtime = lsl::local_clock() + duration; lsl::local_clock() < endtime;) {
			// run a single execution of the inlet
			std::cout << "new inlet(" << name << "," << type << ")...";
			lsl::stream_inlet inlet(result, max_buffered);
			std::cout << "done." << std::endl;
			int numchans = inlet.info().channel_count();
			init_sample(numchans * (int)ceil(max_chunk_len_ms * result.nominal_srate() / 1000 *
			                                 max_chunk_oversize_factor),
			            chunk);
			if (request_info) { std::cout << "  info = " << inlet.info(1.0).name() << std::endl; }
			for (double fail_at = lsl::local_clock() + seconds_between_failures;
				 lsl::local_clock() < fail_at;) {
				std::this_thread::sleep_for(
					std::chrono::milliseconds(1 + rand() % max_inlet_poll_interval_ms));
				inlet.pull_chunk_multiplexed(&chunk[0], nullptr, chunk.size(), 0);
				if (request_time) t = inlet.time_correction(1.0);
			}
			std::cout << "del inlet(" << name << "," << type << ")" << std::endl;
			if (request_time) std::cout << "  tcorr = " << t << std::endl;
			// downtime
			sleep(.1 * (rand() % 50));
		}
	} catch (lsl::timeout_error&) {
		std::cerr << "Timeout exceeded; stopping inlet." << std::endl;
	} catch (lsl::lost_error&) {
		std::cerr << "Found no matching outlet; stopping inlet." << std::endl;
	} catch (std::exception& e) {
		std::cerr << "ERROR during run_inlet() Stress-test function: " << e.what() << std::endl;
	}
	num_inlets.fetch_sub(1);
}

void random_inlets(double spawn_every = 0.0, double duration = 0.0, std::string name = "",
                   std::string type = "", int in_chunks = -1, int request_info = -1,
                   int request_time = -1, double seconds_between_failures = 0.0) {
	if (spawn_every == 0.0) spawn_every = spawn_inlet_interval;
	std::vector<std::thread> threads;
	while (true) {
		try {
			if (num_inlets.load() < max_outlets)
				threads.emplace_back(&run_inlet, duration, name, type, in_chunks, request_info,
				                     request_time, seconds_between_failures);

		} catch (std::exception& e) {
			std::cerr << "Could not spawn a new inlet thread: " << e.what() << std::endl;
		}
		sleep(spawn_every);
		for (auto& t : threads)
			if (t.joinable()) t.join();
	}
}

void random_outlets(double spawn_every = 0.0, double duration = 0.0, std::string name = "",
                    std::string type = "", int numchan = 0,
                    lsl::channel_format_t fmt = lsl::cf_undefined, double srate = 0.0,
                    double seconds_between_failures = 0.0, int chunk_len = 0) {
	std::vector<std::thread> threads;
	if (spawn_every == 0.0) spawn_every = spawn_outlet_interval;
	while (true) {
		try {
			if (num_outlets < max_inlets)
				threads.emplace_back(&run_outlet, duration, name, type, numchan, fmt, srate,
				                     seconds_between_failures, chunk_len);
		} catch (std::exception& e) {
			std::cerr << "Could not spawn a new outlet thread: " << e.what() << std::endl;
		}
		sleep(spawn_every);
		for (auto& t : threads)
			if (t.joinable()) t.join();
	}
}

int main(int argc, char* argv[]) {
	srand((unsigned)time(nullptr));
	std::cout << "This stress test program puts heavy load on network equipment," << std::endl;
	std::cout << "particularly when multiple instances run on the same network." << std::endl;
	std::cout << "We recommend to not run this software on a corporate or campus" << std::endl;
	std::cout << "network since it generates erratic heavy traffic that can " << std::endl;
	std::cout << "alert network operators and/or may crash unreliable equipment." << std::endl << std::endl;
	std::cout << "Are you sure you want to continue? [y/n] (add -f to skip this prompt)" << std::endl;
	if ((argc > 1 && std::string(argv[1]) == "-f") || tolower(std::cin.get()) == 'y') {
		std::thread outlets([]() { random_outlets(); });
		std::thread inlets([]() { random_inlets(); });
		outlets.join();
		inlets.join();
		std::cout << "Press ENTER to exit. " << std::endl;
		std::cin.get();
	}
	return 0;
}
