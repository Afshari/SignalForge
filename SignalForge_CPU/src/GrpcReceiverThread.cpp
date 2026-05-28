#include "GrpcReceiverThread.h"

#include <fstream>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <random>
#include <filesystem>

#pragma warning(push)
#pragma warning(disable: 4996)
#include <grpcpp/grpcpp.h>
#pragma warning(pop)

#include "SignalForge.pb.h"

namespace SignalForge
{

    // --------------------------------------------------------------------------
    // SignalForgeServiceImpl
    // --------------------------------------------------------------------------

    SignalForgeServiceImpl::SignalForgeServiceImpl(ThreadSafeQueue<std::string>& path_queue,
        const std::string& input_dir)
        : m_path_queue(path_queue)
        , m_input_dir(input_dir)
    {
    }

    std::string SignalForgeServiceImpl::GenerateFilename()
    {
        // unix timestamp
        auto now = std::chrono::system_clock::now();
        auto seconds = std::chrono::duration_cast<std::chrono::seconds>(
            now.time_since_epoch()).count();

        // random hex suffix
        std::random_device              rd;
        std::mt19937                    gen(rd());
        std::uniform_int_distribution<> dist(0, 0xFFFFFF);
        std::ostringstream              oss;
        oss << seconds << "_" << std::hex << std::setw(6) << std::setfill('0') << dist(gen) << ".wav";
        return oss.str();
    }

    grpc::Status SignalForgeServiceImpl::SendFile(grpc::ServerContext*  /*context*/,
        const FileRequest* request,
        FileResponse* response)
    {
        if (request->data().empty())
        {
            response->set_accepted(false);
            response->set_message("empty payload");
            return grpc::Status::OK;
        }

        // generate unique path and write bytes to disk
        std::string filename = GenerateFilename();
        std::string filepath = m_input_dir + "/" + filename;

        std::ofstream ofs(filepath, std::ios::binary);
        if (!ofs.is_open())
        {
            response->set_accepted(false);
            response->set_message("failed to open output file");
            return grpc::Status::OK;
        }

        ofs.write(request->data().data(), static_cast<std::streamsize>(request->data().size()));
        ofs.close();

        // push path into pipeline queue
        m_path_queue.push(filepath);

        response->set_accepted(true);
        response->set_message("ok");
        return grpc::Status::OK;
    }


    // --------------------------------------------------------------------------
    // GrpcReceiverThread
    // --------------------------------------------------------------------------

    GrpcReceiverThread::GrpcReceiverThread(ThreadSafeQueue<std::string>& path_queue,
        const std::string& input_dir,
        const std::string& listen_address)
        : m_path_queue(path_queue)
        , m_input_dir(input_dir)
        , m_listen_address(listen_address)
    {
    }

    void GrpcReceiverThread::Start()
    {
        m_thread = std::thread(&GrpcReceiverThread::Run, this);
    }

    void GrpcReceiverThread::Stop()
    {
        {
            std::unique_lock<std::mutex> lock(m_server_mutex);
            m_server_cv.wait(lock, [this] { return m_server_ready; });
        }
        if (m_server)
        {
            m_server->Shutdown();
        }
        if (m_thread.joinable())
        {
            m_thread.join();
        }
        if (!m_path_queue.is_closed())
            m_path_queue.close();
    }

    void GrpcReceiverThread::Run()
    {
        SignalForgeServiceImpl service(m_path_queue, m_input_dir);

        grpc::ServerBuilder builder;
        builder.AddListeningPort(m_listen_address, grpc::InsecureServerCredentials());
        builder.RegisterService(&service);

        m_server = builder.BuildAndStart();

        {
            std::lock_guard<std::mutex> lock(m_server_mutex);
            m_server_ready = true;
        }
        m_server_cv.notify_one();

        if (m_server)
        {
            m_server->Wait();
        }
    }

    grpc::Status SignalForgeServiceImpl::Register(grpc::ServerContext* /*context*/,
        const RegisterRequest* /*request*/,
        RegisterResponse* response)
    {
        // generate unique client_id
        auto now = std::chrono::system_clock::now();
        auto seconds = std::chrono::duration_cast<std::chrono::seconds>(
            now.time_since_epoch()).count();

        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dist(0, 0xFFFFFF);
        std::ostringstream oss;
        oss << "client_" << seconds << "_"
            << std::hex << std::setw(6) << std::setfill('0') << dist(gen);
        std::string client_id = oss.str();

        {
            std::lock_guard<std::mutex> lock(m_clients_mutex);
            m_registered_clients.insert(client_id);
        }

        response->set_accepted(true);
        response->set_client_id(client_id);
        return grpc::Status::OK;
    }

    grpc::Status SignalForgeServiceImpl::Shutdown(grpc::ServerContext* /*context*/,
        const ShutdownRequest* request,
        ShutdownResponse* response)
    {
        {
            std::lock_guard<std::mutex> lock(m_clients_mutex);
            m_registered_clients.erase(request->client_id());

            if (m_registered_clients.empty())
            {
                // last client disconnected - close the queue
                m_path_queue.close();
            }
        }

        response->set_accepted(true);
        response->set_message("ok");
        return grpc::Status::OK;
    }

} // namespace SignalForge