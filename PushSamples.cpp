#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <lsl_cpp.h>
#include <map>
#include <string>

using std::chrono::milliseconds;

template <typename T>
void push_fn(const std::vector<T>& buffer, bool push_single, uint32_t chunksize, lsl::stream_outlet& out) {
	if (push_single)
		for (uint32_t i = 0; i < chunksize; ++i) out.push_sample(buffer.data());
	else
		out.push_chunk_multiplexed(buffer, out.info().channel_count() * chunksize);
}
template <typename T>
void pull_fn(std::vector<T>& buffer, bool pull_single, uint32_t chunksize, uint32_t numchans, lsl::stream_inlet& in) {
	if (pull_single)
		for (uint32_t i = 0; i < chunksize; ++i) in.pull_sample(buffer.data(), numchans);
	else
		in.pull_chunk_multiplexed(buffer.data(), nullptr, numchans * chunksize, 0);
}

int main(int argc, char* argv[]) {
	if (argc == 2 && (argv[1] == std::string("-h") || argv[1] == std::string("--help"))) {
		std::cout << "Usage: " << argv[0] << ": [numchans=32] [format_str=0] [maxsamples=500000]"
				  << "[chunksize=100] "
				  << "[push_single=0] "
				  << "[pull_single=0] " << std::endl;
		std::cout << "\tformat_str: 0 for float, 1 for string samples\n";
		std::cout << "\tpush_single / pull_single: 0 for chunked operation, "
				  << "1 for handling each sample on its own (likely much slower)\n\n";
		std::cout << "Example:\n\t ./push_sample 5 1\n\t"
				  << "Pushes the default number of samples in 5 string channels" << std::endl;
		return 0;
	}
	const uint32_t numchans = argc > 1 ? std::stoul(argv[1]) : 32;
	const auto format_str = argc > 2 && *argv[2] == '1';
	const uint32_t maxsamples = argc > 3 ? std::stoul(argv[3]) : 500000;
	const uint32_t chunksize = argc > 4 ? std::stoul(argv[4]) : 100;
	const bool push_single = argc > 5 && *argv[5] == '1';
	const bool pull_single = argc > 6 && *argv[6] == '1';

	std::cout << "LSL version info: " << lsl::library_info() << std::endl;
	std::cout << "Starting speed test with " << numchans << " channels ("
			  << (format_str ? "string" : "float32") << "), " << maxsamples << " samples\n"
			  << "Pushing single samples: " << push_single
			  << "\nPulling single samples: " << pull_single << std::endl;
	{
		const auto name = std::string("PushSamples_") + (format_str ? "str" : "float") + 'x' +
						  std::to_string(numchans);
		std::cout << "Publishing stream " << name << std::endl;

		lsl::stream_info info(name, "Benchmark", numchans, lsl::IRREGULAR_RATE,
			format_str ? lsl::cf_string : lsl::cf_float32);
		lsl::stream_outlet outlet(info);

		auto inlets = lsl::resolve_stream("name", name, 1);
		if(inlets.empty()) {
			std::cout << "Outlet not found" << std::endl;
			return 1;
		}
		lsl::stream_inlet inlet(inlets[0]);
		inlet.open_stream(2.);
		outlet.wait_for_consumers(2.);

		const uint32_t buffersize = numchans * chunksize;
		std::vector<float> samples_float(format_str ? 0 : buffersize, 17.3f);
		std::vector<std::string> samples_str(format_str ? buffersize : 0, "test");

		double outlettime = 0, inlettime = 0;
		for (uint32_t chunk = 0; chunk < maxsamples / chunksize; chunk++) {
			double starttime = lsl::local_clock();
			if (format_str)
				push_fn(samples_str, push_single, chunksize, outlet);
			else
				push_fn(samples_float, push_single, chunksize, outlet);
			outlettime += lsl::local_clock() - starttime;

			starttime = lsl::local_clock();
			if (format_str)
				pull_fn(samples_str, pull_single, chunksize, numchans, inlet);
			else
				pull_fn(samples_float, pull_single, chunksize, numchans, inlet);
			inlettime += lsl::local_clock() - starttime;
		}

		auto printstats =
			[&](std::ostream &os, std::string name, double time, bool single) {
				os << name << ": " << maxsamples << " samples in " << static_cast<int>(time * 1000)
				   << " ms (" << static_cast<int>(maxsamples / time) << " samples/s)" << std::endl;
				if (!single)
					std::cout << name << ": " << (maxsamples / chunksize) << " ops in "
							  << static_cast<int>(time * 1000) << " ms ("
							  << static_cast<int>(maxsamples / time / chunksize) << " ops/s)" << std::endl;
			};
		printstats(std::cout, "Inlet", inlettime, pull_single);
		printstats(std::cout, "Outlet", outlettime, push_single);
	}
	if (argc == 1) {
		std::cout << "Press [Enter] to exit" << std::endl;
		std::cin.get();
	}
	return 0;
}
