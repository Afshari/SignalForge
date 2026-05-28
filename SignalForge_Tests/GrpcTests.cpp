#include "pch.h"
#include "GrpcReceiverThread.h"
#include "ThreadSafeQueue.h"
#include "TestHelpers.h"
#include <filesystem>
#include <fstream>
#include <thread>
#include <chrono>

#pragma warning(push)
#pragma warning(disable: 4996)
#include <grpcpp/grpcpp.h>
#include "SignalForge.grpc.pb.h"
#include "SignalForge.pb.h"
#pragma warning(pop)

namespace SignalForge {

    class GrpcTest : public ::testing::Test
    {
    protected:
        ThreadSafeQueue<std::string>        m_queue;
        std::unique_ptr<GrpcReceiverThread> m_receiver;
        std::filesystem::path               m_input_dir;

        static constexpr const char* k_address = "0.0.0.0:50052";

        void SetUp() override
        {
            m_input_dir = std::filesystem::current_path() / "grpc_test_input";
            std::filesystem::create_directories(m_input_dir);

            m_receiver = std::make_unique<GrpcReceiverThread>(
                m_queue,
                m_input_dir.string(),
                k_address);
            m_receiver->Start();

            // give the server a moment to start
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        void TearDown() override
        {
            if(m_receiver)
                m_receiver->Stop();
            std::filesystem::remove_all(m_input_dir);
        }
    };

    TEST_F(GrpcTest, GrpcReceiver_StartsAndStopsCleanly)
    {
        // nothing to do - SetUp started it, TearDown will stop it
        // if we reach here without hanging or crashing, the test passes
        SUCCEED();
    }

    TEST_F(GrpcTest, GrpcReceiver_SendFile_WritesFileToDisk)
    {
        // build a minimal WAV in memory
        std::vector<int16_t> samples(1024, 0);
        std::filesystem::path tmp = std::filesystem::current_path() / "grpc_tmp.wav";
        TestHelpers::WriteMinimalWav(tmp, samples);

        // read bytes
        std::ifstream ifs(tmp, std::ios::binary);
        std::vector<char> bytes((std::istreambuf_iterator<char>(ifs)),
            std::istreambuf_iterator<char>());
        ifs.close();
        std::filesystem::remove(tmp);

        // send via gRPC
        auto channel = grpc::CreateChannel("localhost:50052", grpc::InsecureChannelCredentials());
        auto stub = SignalForgeService::NewStub(channel);

        FileRequest  request;
        FileResponse response;
        grpc::ClientContext ctx;

        request.set_data(std::string(bytes.begin(), bytes.end()));
        stub->SendFile(&ctx, request, &response);

        EXPECT_TRUE(response.accepted());

        // verify a file appeared in input_dir
        int file_count = 0;
        for (const auto& entry : std::filesystem::directory_iterator(m_input_dir))
            if (entry.path().extension() == ".wav")
                file_count++;

        EXPECT_EQ(file_count, 1);
    }

    TEST_F(GrpcTest, GrpcReceiver_SendFile_PushesPathToQueue)
    {
        // build a minimal WAV in memory
        std::vector<int16_t> samples(1024, 0);
        std::filesystem::path tmp = std::filesystem::current_path() / "grpc_tmp.wav";
        TestHelpers::WriteMinimalWav(tmp, samples);

        // read bytes
        std::ifstream ifs(tmp, std::ios::binary);
        std::vector<char> bytes((std::istreambuf_iterator<char>(ifs)),
            std::istreambuf_iterator<char>());
        ifs.close();
        std::filesystem::remove(tmp);

        // send via gRPC
        auto channel = grpc::CreateChannel("localhost:50052", grpc::InsecureChannelCredentials());
        auto stub = SignalForgeService::NewStub(channel);

        FileRequest  request;
        FileResponse response;
        grpc::ClientContext ctx;

        request.set_data(std::string(bytes.begin(), bytes.end()));
        stub->SendFile(&ctx, request, &response);

        EXPECT_TRUE(response.accepted());

        // verify path was pushed into queue
        auto path = m_queue.pop();
        ASSERT_TRUE(path.has_value());
        EXPECT_FALSE(path->empty());
        EXPECT_EQ(std::filesystem::path(*path).extension(), ".wav");
    }

    TEST_F(GrpcTest, GrpcReceiver_SendFile_EmptyPayload_ReturnsRejected)
    {
        auto channel = grpc::CreateChannel("localhost:50052", grpc::InsecureChannelCredentials());
        auto stub = SignalForgeService::NewStub(channel);

        FileRequest  request;
        FileResponse response;
        grpc::ClientContext ctx;

        // send empty payload
        request.set_data("");
        stub->SendFile(&ctx, request, &response);

        EXPECT_FALSE(response.accepted());
        EXPECT_EQ(response.message(), "empty payload");
    }

    TEST_F(GrpcTest, GrpcReceiver_Stop_SentinelReachesQueue)
    {
        // stop the receiver - sentinel should be pushed into queue
        m_receiver->Stop();

        auto item = m_queue.pop();
        ASSERT_TRUE(item.has_value());
        EXPECT_TRUE(item->empty());

        // prevent TearDown from calling Stop() again
        m_receiver.reset();
    }

} // namespace SignalForge