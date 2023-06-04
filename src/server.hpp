#include <deque>
#include <kutils.hpp>
#include <kproto/ipc.hpp>

//----------------------------------------------------------------
namespace kiq::katrix
{
using ipc_msg_t = ipc_message::u_ipc_msg_ptr;
//-------------------------------------------------------------
struct request_t
{
  std::string user;
  std::string text;
  std::string media;
  std::string time;
};
//-------------------------------------------------------------
class request_converter
{
public:
  request_converter() = default;
  request_t receive(ipc_msg_t msg);

private:
using msg_handler_t = std::function<void(ipc_msg_t)>;
using dispatch_t    = std::map<uint8_t, msg_handler_t>;

  void on_request(ipc_msg_t msg);

  request_t  req;
  dispatch_t m_dispatch_table{
    {constants::IPC_KIQ_MESSAGE,   [this](ipc_msg_t msg) {                             }},
    {constants::IPC_PLATFORM_TYPE, [this](ipc_msg_t msg) { on_request(std::move(msg)); }}
  };
}; // request_converter
//-------------------------------------------------------------
class server
{
public:
  server();
  ~server();

  bool      is_active()                    const;
  bool      has_msgs ()                    const;
  void      reply    (bool success = true);
  ipc_msg_t get_msg  ();

private:
  void run();

  void recv();

  zmq::context_t             context_;
  zmq::socket_t              rx_;
  zmq::socket_t              tx_;
  std::future<void>          future_;
  bool                       active_{true};
  uint32_t                   replies_pending_{0};
  std::deque<ipc_msg_t>      msgs_;
  std::vector<std::string>   processed_;
}; // server
} // ns kiq::katrix