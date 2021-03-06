#ifndef OBELISK_CLIENT_BACKEND
#define OBELISK_CLIENT_BACKEND

#include <functional>
#include <unordered_map>
#include <system_error>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <zmq.hpp>
#include <obelisk/message.hpp>
#include <bitcoin/threadpool.hpp>

namespace obelisk {

typedef bc::data_chunk worker_uuid;

class backend_cluster
{
public:
    typedef std::function<void (
        const bc::data_chunk&, const worker_uuid&)> response_handler;

    backend_cluster(bc::threadpool& pool,
        zmq::context_t& context, const std::string& connection);

    void request(
        const std::string& command, const bc::data_chunk& data,
        response_handler handle, const worker_uuid& dest=worker_uuid());
    void update();

    void append_filter(const std::string& command, response_handler filter);

private:
    struct request_container
    {
        boost::posix_time::ptime timestamp;
        boost::posix_time::time_duration timeout;
        size_t retries_left;
        outgoing_message message;
    };

    typedef std::unordered_map<uint32_t, response_handler>
        response_handler_map;
    typedef std::unordered_map<uint32_t, request_container>
        request_retry_queue;

    typedef std::unordered_map<std::string, response_handler> filter_map;

    void send(const outgoing_message& message);
    void receive_incoming();

    void process(const incoming_message& response);
    bool process_filters(const incoming_message& response);
    bool process_as_reply(const incoming_message& response);

    void resend_expired();

    zmq::context_t& context_;
    zmq::socket_t socket_;
    // Requests
    bc::async_strand strand_;
    response_handler_map handlers_;
    request_retry_queue retry_queue_;
    // Preprocessing filters. If any of these succeed then process() stops.
    filter_map filters_;
};

} // namespace obelisk

#endif

