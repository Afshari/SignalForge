#include "Config.h"
#include <boost/json.hpp>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace json = boost::json;

namespace SignalForge {

    Config Config::Load(const std::filesystem::path& filepath)
    {
        std::ifstream file(filepath);
        if (!file.is_open())
            throw std::runtime_error("Config file not found: " + filepath.string());

        std::stringstream buffer;
        buffer << file.rdbuf();

        json::value root = json::parse(buffer.str());

        Config config;
        config.max_file_size_kb     = root.at("file").at("max_file_size_kb").as_int64();
        config.sample_rate          = root.at("file").at("sample_rate").as_int64();

        config.sha256.batch_size        = root.at("kernels").at("sha256").at("batch_size").as_int64();
        config.sha256.threads_per_block = root.at("kernels").at("sha256").at("threads_per_block").as_int64();

        config.fft.batch_size           = root.at("kernels").at("fft").at("batch_size").as_int64();
        config.fft.threads_per_block    = root.at("kernels").at("fft").at("threads_per_block").as_int64();
        config.fft_size                 = root.at("kernels").at("fft").at("fft_size").as_int64();

        if (root.as_object().contains("pipeline"))
            config.reader_threads = root.at("pipeline").at("reader_threads").as_int64();
        else
            config.reader_threads = 1;

        if (root.as_object().contains("verbose"))
            config.verbose = root.at("verbose").as_bool();
        else
            config.verbose = false;

        config.redis.host = root.at("redis").at("host").as_string().c_str();
        config.redis.port = root.at("redis").at("port").as_int64();

        config.test_data_dir  = root.at("paths").at("test_data_dir").as_string().c_str();
        config.output_dir     = root.at("paths").at("output_dir").as_string().c_str();
        config.input_dir      = root.at("paths").at("input_dir").as_string().c_str();

        return config;
    }

    Config Config::Default()
    {
        Config config;
        config.max_file_size_kb         = 2048;
        config.sample_rate              = 44100;

        config.sha256.batch_size        = 512;
        config.sha256.threads_per_block = 128;

        config.fft.batch_size           = 4096;
        config.fft.threads_per_block    = 256;
        config.fft_size                 = 8192;

        config.reader_threads = 4;
        config.verbose = false;

        config.redis.host = "127.0.0.1";
        config.redis.port = 6379;

        config.test_data_dir  = "test_data";
        config.output_dir     = "output";
        config.input_dir      = "input";
        return config;
    }

} // namespace SignalForge