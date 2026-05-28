#pragma once
#include <vector>
#include <string>
#include "ThreadSafeQueue.h"
#include "WavBatch.h"
#include "FFTResult.h"
#include "Config.h"
#include "GrpcReceiverThread.h"

namespace SignalForge {

    class SignalForgePipeline
    {
    public:
        // filepaths: list of .wav files to process (from local directory scan)
        // config: batch sizes, fft_size, Redis connection, etc.
        SignalForgePipeline(const std::vector<std::string>& filepaths,
            Config config);

        // Launches all pipeline threads and blocks until processing is complete.
        // Thread order:
        //   1. Scanner thread  - pushes filepaths into m_path_queue
        //   2. Reader thread(s)- read PCM data, push WavBatch into m_wav_queue
        //   3. GPU worker      - SHA-256 filter, FFT, push FFTResult into m_result_queue
        //   4. Redis writer    - stores hashes and magnitudes
        void Run();

    private:
        std::vector<std::string>     m_filepaths;
        Config                       m_config;

        // Stage 0->1: file paths
        // gRPC will push into this queue later as a second producer
        ThreadSafeQueue<std::string> m_path_queue;

        // Stage 1->2: raw PCM batches ready for GPU
        ThreadSafeQueue<WavBatch>    m_wav_queue;

        // Stage 2->3: processed results ready for Redis
        ThreadSafeQueue<FFTResult>   m_result_queue;

        std::unique_ptr<GrpcReceiverThread> m_grpc_receiver;
    };

} // namespace SignalForge