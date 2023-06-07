#include <deque>
#include <kutils.hpp>
#include <kproto/ipc.hpp>
#include <variant>

//----------------------------------------------------------------
namespace kiq::katrix
{
using ipc_msg_t = ipc_message::u_ipc_msg_ptr;
//-------------------------------------------------------------
struct request_t
{
  std::string id;
  std::string user;
  std::string text;
  std::string media;
  std::string time;
  bool        info{false};
};
//-------------------------------------------------------------
using ipc_var_t     = std::variant<kiq::platform_message, std::unique_ptr<kiq::platform_message>, std::unique_ptr<kiq::platform_info>, kiq::platform_info, ipc_msg_t>;
class request_converter
{
public:
  request_converter() = default;
  request_t receive(ipc_msg_t msg);

private:
using msg_handler_t = std::function<void(ipc_msg_t)>;
using dispatch_t    = std::map<uint8_t, msg_handler_t>;

  void on_request(ipc_var_t);

  request_t  req;
  dispatch_t m_dispatch_table{
    {constants::IPC_KIQ_MESSAGE,   [this](ipc_msg_t msg) {                             }},
    {constants::IPC_PLATFORM_TYPE, [this](ipc_msg_t msg) { on_request(*(static_cast<platform_message*>(msg.get()))); }},
    {constants::IPC_PLATFORM_INFO, [this](ipc_msg_t msg) { on_request(*(static_cast<platform_info*>   (msg.get()))); }}
  };
}; // request_converter
//-------------------------------------------------------------
class server
{
using msg_id_t = std::string;
public:
  server();
  ~server();

  bool      is_active()                    const;
  bool      has_msgs ()                    const;
  void      reply    (const request_t& req, bool success = true);
  ipc_msg_t get_msg  ();

private:
  void run();

  void recv();

  zmq::context_t                context_;
  zmq::socket_t                 rx_;
  zmq::socket_t                 tx_;
  std::future<void>             future_;
  bool                          active_{true};
  uint32_t                      replies_pending_{0};
  std::deque<ipc_msg_t>         msgs_;
  std::vector<std::string>      processed_;
  std::map<msg_id_t, ipc_msg_t> pending_;
}; // server
} // ns kiq::katrix
