#include "worker.hpp"

#include <bitcoin/format.hpp>
#include <bitcoin/utility/logger.hpp>
#include "echo.hpp"

namespace obelisk {

using namespace bc;
namespace posix_time = boost::posix_time;
using posix_time::milliseconds;
using posix_time::seconds;
using posix_time::microsec_clock;

const posix_time::time_duration heartbeat_interval = milliseconds(1000);
constexpr size_t interval_init = 4, interval_max = 32;

auto now = []() { return microsec_clock::universal_time(); };

bool send_string(zmq::socket_t& socket, const std::string& str)
{
    zmq::message_t message(str.size());
    char* msg_begin = reinterpret_cast<char*>(message.data());
    std::copy(str.begin(), str.end(), msg_begin);
    return socket.send(message);
}

request_worker::request_worker()
  : context_(1)
{
}
bool request_worker::start(config_map_type& config)
{
    connection_ = config["service"];
    name_ = config["name"];
    create_new_socket();
    last_heartbeat_ = now();
    heartbeat_at_ = now() + heartbeat_interval;
    interval_ = interval_init;
}

void request_worker::create_new_socket()
{
    socket_ = std::make_shared<zmq::socket_t>(context_, ZMQ_DEALER);
    log_debug(LOG_WORKER) << "Connecting: " << connection_;
    // Set the socket identity name.
    if (!name_.empty())
        socket_->setsockopt(ZMQ_IDENTITY, name_.c_str(), name_.size());
    // Connect...
    socket_->connect(connection_.c_str());
    // Configure socket to not wait at close time
    int linger = 0;
    socket_->setsockopt(ZMQ_LINGER, &linger, sizeof (linger));
    // Tell queue we're ready for work
    log_info(LOG_WORKER) << "worker ready";
    send_string(*socket_, "READY");
}

void request_worker::attach(
    const std::string& command, command_handler handler)
{
    handlers_[command] = handler;
}

void request_worker::update()
{
    zmq::pollitem_t items [] = { { *socket_,  0, ZMQ_POLLIN, 0 } };
    int rc = zmq::poll(items, 1, 50000);
    BITCOIN_ASSERT(rc >= 0);

    if (items[0].revents & ZMQ_POLLIN)
    {
        // Get message
        // - 6-part envelope + content -> request
        // - 1-part "HEARTBEAT" -> heartbeat
        incoming_message request;
        request.recv(*socket_);

        if (!request.is_signal())
        {
            last_heartbeat_ = now();
            auto it = handlers_.find(request.command());
            // Perform request if found.
            if (it != handlers_.end())
            {
                log_debug(LOG_WORKER)
                    << request.command() << " from " << request.origin();
                it->second(request, socket_);
            }
            else
            {
                log_warning(LOG_WORKER)
                    << "Unhandled request: " << request.command()
                    << " from " << request.origin();
            }
        }
        else if (request.command() == "HEARTBEAT")
        {
            log_debug(LOG_WORKER) << "Received heartbeat";
            last_heartbeat_ = now();
        }
        else
        {
            log_warning(LOG_WORKER) << "invalid message";
        }
        interval_ = interval_init;
    }
    else if (now() - last_heartbeat_ > seconds(interval_))
    {
        log_warning(LOG_WORKER) << "heartbeat failure, can't reach queue";
        log_warning(LOG_WORKER) << "reconnecting in "
            << interval_ << " seconds...";

        if (interval_ < interval_max)
        {
            interval_ *= 2;
        }
        create_new_socket();
        last_heartbeat_ = now();
    }

    // Send heartbeat to queue if it's time
    if (now() > heartbeat_at_)
    {
        heartbeat_at_ = now() + heartbeat_interval;
        log_debug(LOG_WORKER) << "Sending heartbeat";
        send_string(*socket_, "HEARTBEAT");
    }
}

} // namespace obelisk

