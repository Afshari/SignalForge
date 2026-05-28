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

        static constexpr const char* k_address = "localhost:50052";

        void SetUp() override
        {
            m_input_dir = std::filesystem::current_path() / "grpc_test_input";
            std::filesystem::create_directories(m_input_dir);

            m_receiver = std::make_unique<GrpcReceiverThread>(
                m_queue,
                m_input_dir.string(),
                "0.0.0.0:50052");
            m_receiver->Start();

            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        void TearDown() override
        {
            if (m_receiver)
                m_receiver->Stop();
            std::filesystem::remove_all(m_input_dir);
        }

        // helper - creates a stub connected to the test server
        std::unique_ptr<SignalForgeService::Stub> MakeStub()
        {
            auto channel = grpc::CreateChannel(k_address, grpc::InsecureChannelCredentials());
            return SignalForgeService::NewStub(channel);
        }

        // helper - registers a client and returns its client_id
        std::string RegisterClient(SignalForgeService::Stub* stub)
        {
            RegisterRequest  request;
            RegisterResponse response;
            grpc::ClientContext ctx;
            stub->Register(&ctx, request, &response);
            return response.client_id();
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

    TEST_F(GrpcTest, GrpcReceiver_Stop_ClosesQueue)
    {
        // no clients registered - Stop() should close the queue
        m_receiver->Stop();

        // queue should be closed - pop should return nullopt
        auto item = m_queue.pop();
        EXPECT_FALSE(item.has_value());

        // prevent TearDown from calling Stop() again
        m_receiver.reset();
    }

    TEST_F(GrpcTest, GrpcReceiver_Shutdown_ReturnsAck)
    {
        auto stub = MakeStub();

        // register first
        std::string client_id = RegisterClient(stub.get());
        ASSERT_FALSE(client_id.empty());

        // shutdown
        ShutdownRequest  request;
        ShutdownResponse response;
        grpc::ClientContext ctx;

        request.set_client_id(client_id);
        stub->Shutdown(&ctx, request, &response);

        EXPECT_TRUE(response.accepted());
        EXPECT_EQ(response.message(), "ok");
    }

    TEST_F(GrpcTest, GrpcReceiver_Shutdown_ClosesQueue)
    {
        auto stub = MakeStub();

        // register first
        std::string client_id = RegisterClient(stub.get());
        ASSERT_FALSE(client_id.empty());

        // shutdown
        ShutdownRequest  request;
        ShutdownResponse response;
        grpc::ClientContext ctx;

        request.set_client_id(client_id);
        stub->Shutdown(&ctx, request, &response);

        EXPECT_TRUE(response.accepted());

        // queue should be closed - pop should return nullopt
        auto item = m_queue.pop();
        EXPECT_FALSE(item.has_value());
    }

    TEST_F(GrpcTest, GrpcReceiver_MultiClient_AllFilesReceived)
    {
        // two clients register
        auto stub1 = MakeStub();
        auto stub2 = MakeStub();

        std::string client_id1 = RegisterClient(stub1.get());
        std::string client_id2 = RegisterClient(stub2.get());

        ASSERT_FALSE(client_id1.empty());
        ASSERT_FALSE(client_id2.empty());

        // build a minimal WAV in memory
        std::vector<int16_t> samples(1024, 0);
        std::filesystem::path tmp = std::filesystem::current_path() / "grpc_tmp_mc.wav";
        TestHelpers::WriteMinimalWav(tmp, samples);

        std::ifstream ifs(tmp, std::ios::binary);
        std::vector<char> bytes((std::istreambuf_iterator<char>(ifs)),
            std::istreambuf_iterator<char>());
        ifs.close();
        std::filesystem::remove(tmp);

        // client 1 sends a file
        {
            FileRequest  request;
            FileResponse response;
            grpc::ClientContext ctx;
            request.set_data(std::string(bytes.begin(), bytes.end()));
            stub1->SendFile(&ctx, request, &response);
            EXPECT_TRUE(response.accepted());
        }

        // client 2 sends a file
        {
            FileRequest  request;
            FileResponse response;
            grpc::ClientContext ctx;
            request.set_data(std::string(bytes.begin(), bytes.end()));
            stub2->SendFile(&ctx, request, &response);
            EXPECT_TRUE(response.accepted());
        }

        // both paths should be in the queue
        auto path1 = m_queue.pop();
        auto path2 = m_queue.pop();

        ASSERT_TRUE(path1.has_value());
        ASSERT_TRUE(path2.has_value());
        EXPECT_EQ(std::filesystem::path(*path1).extension(), ".wav");
        EXPECT_EQ(std::filesystem::path(*path2).extension(), ".wav");

        // cleanup - shutdown both clients
        {
            ShutdownRequest  request;
            ShutdownResponse response;
            grpc::ClientContext ctx;
            request.set_client_id(client_id1);
            stub1->Shutdown(&ctx, request, &response);
        }
        {
            ShutdownRequest  request;
            ShutdownResponse response;
            grpc::ClientContext ctx;
            request.set_client_id(client_id2);
            stub2->Shutdown(&ctx, request, &response);
        }
    }

    TEST_F(GrpcTest, GrpcReceiver_MultiClient_ShutdownOnlyWhenAllDisconnect)
    {
        // two clients register
        auto stub1 = MakeStub();
        auto stub2 = MakeStub();

        std::string client_id1 = RegisterClient(stub1.get());
        std::string client_id2 = RegisterClient(stub2.get());

        ASSERT_FALSE(client_id1.empty());
        ASSERT_FALSE(client_id2.empty());

        // first client shuts down - queue should stay open
        {
            ShutdownRequest  request;
            ShutdownResponse response;
            grpc::ClientContext ctx;
            request.set_client_id(client_id1);
            stub1->Shutdown(&ctx, request, &response);
            EXPECT_TRUE(response.accepted());
        }

        // queue should still be open - push and pop should work
        m_queue.push("test_path");
        auto item = m_queue.pop();
        ASSERT_TRUE(item.has_value());
        EXPECT_EQ(*item, "test_path");

        // second client shuts down - queue should now close
        {
            ShutdownRequest  request;
            ShutdownResponse response;
            grpc::ClientContext ctx;
            request.set_client_id(client_id2);
            stub2->Shutdown(&ctx, request, &response);
            EXPECT_TRUE(response.accepted());
        }

        // queue should be closed now - pop should return nullopt
        auto final_item = m_queue.pop();
        EXPECT_FALSE(final_item.has_value());
    }

} // namespace SignalForge