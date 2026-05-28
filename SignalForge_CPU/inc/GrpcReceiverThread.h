#pragma once

#include <string>
#include <memory>
#include <thread>
#include <set>

#include "ThreadSafeQueue.h"

#pragma warning(push)
#pragma warning(disable: 4996)
#include <grpcpp/grpcpp.h>
#include "SignalForge.grpc.pb.h"
#pragma warning(pop)


namespace SignalForge
{

    // gRPC service implementation -- receives files and saves them to input_dir
    class SignalForgeServiceImpl final : public SignalForgeService::Service
    {
    public:
        SignalForgeServiceImpl(ThreadSafeQueue<std::string>& path_queue,
            const std::string& input_dir);

        grpc::Status SendFile(grpc::ServerContext* context,
            const FileRequest* request,
            FileResponse* response) override;

        grpc::Status Register(grpc::ServerContext* context,
            const RegisterRequest* request,
            RegisterResponse* response) override;

        grpc::Status Shutdown(grpc::ServerContext* context,
            const ShutdownRequest* request,
            ShutdownResponse* response) override;

    private:
        ThreadSafeQueue<std::string>& m_path_queue;
        std::string                   m_input_dir;

        std::mutex              m_clients_mutex;
        std::set<std::string>   m_registered_clients;

        // generates a unique filename: <unix_timestamp>_<random_hex>.wav
        static std::string GenerateFilename();
    };


    // wrapper that owns the gRPC server and runs it on a background thread
    class GrpcReceiverThread
    {
    public:
        GrpcReceiverThread(ThreadSafeQueue<std::string>& path_queue,
            const std::string& input_dir,
            const std::string& listen_address);

        void Start();
        void Stop();

    private:
        void Run();

        ThreadSafeQueue<std::string>& m_path_queue;
        std::string                   m_input_dir;
        std::string                   m_listen_address;

        std::unique_ptr<grpc::Server> m_server;
        std::thread                   m_thread;

        std::mutex                    m_server_mutex;
        std::condition_variable       m_server_cv;
        bool                          m_server_ready;
    };

} // namespace SignalForge