#include "server.hpp"
#include <logger.hpp>

static const char* RX_ADDR{"tcp://0.0.0.0:28477"};
static const char* TX_ADDR{"tcp://0.0.0.0:28478"};
static const char* g_platform{"Matrix"};
//----------------------------------------------------------------
namespace kiq::katrix
{
//-------------------------------------------------------------
request_t request_converter::receive(ipc_msg_t msg)
{
  const auto type = msg->type();

  if (type >= constants::IPC_PLATFORM_TYPE)
    m_dispatch_table[type](std::move(msg));

  return req;
}
//----------------------------------
struct req_parser
{
request_t operator()(kiq::platform_message msg)
{
  return request_t{
    .id    = msg.id     (),
    .user  = msg.user   (),
    .text  = msg.content(),
    .media = msg.urls   (),
    .time  = msg.time   ()};
}
//----------------------------------
request_t operator()(kiq::platform_info msg)
{
  request_t req;
  req.info = true;
  req.text = msg.type();
  req.id   = msg.type();
  return req;
}

};
//----------------------------------
void request_converter::on_request(ipc_var_t msg)
{
  static req_parser parser;
  std::visit([this](auto msg) { req = parser(msg); }, msg);
}
//-------------------------------------------------------------
server::server()
: context_{1},
  rx_(context_, ZMQ_ROUTER),
  tx_(context_, ZMQ_DEALER)
{
  rx_.set(zmq::sockopt::linger, 0);
  tx_.set(zmq::sockopt::linger, 0);
  rx_.set(zmq::sockopt::routing_id, "katrix_daemon");
  tx_.set(zmq::sockopt::routing_id, "katrix_daemon_tx");
  rx_.set(zmq::sockopt::tcp_keepalive, 1);
  tx_.set(zmq::sockopt::tcp_keepalive, 1);
  rx_.set(zmq::sockopt::tcp_keepalive_idle,  300);
  tx_.set(zmq::sockopt::tcp_keepalive_idle,  300);
  rx_.set(zmq::sockopt::tcp_keepalive_intvl, 300);
  tx_.set(zmq::sockopt::tcp_keepalive_intvl, 300);

  rx_.bind   (RX_ADDR);
  tx_.connect(TX_ADDR);

  future_ = std::async(std::launch::async, [this] { run(); });
  kiq::log::klog().i("Server listening on ", RX_ADDR);

  kiq::set_log_fn([](const char* message) { kiq::log::klog().t(message); });
}
//----------------------------------
server::~server()
{
  active_ = false;
  if (future_.valid())
    future_.wait();
}
//----------------------------------
bool server::is_active() const
{
  return active_;
}
//----------------------------------
ipc_msg_t server::get_msg()
{
  ipc_msg_t msg = std::move(msgs_.front());
  msgs_.pop_front();

  if      (msg->type() == constants::IPC_PLATFORM_INFO)
    pending_[static_cast<platform_info*>(msg.get())->type()]  = ipc_message::clone(*msg);
  else if (msg->type() == constants::IPC_PLATFORM_TYPE)
    pending_[static_cast<platform_message*>(msg.get())->id()] = ipc_message::clone(*msg);

  return msg;
}
//----------------------------------
bool server::has_msgs() const
{
  return !msgs_.empty();
}
//----------------------------------
void server::reply(const request_t& req, bool success)
{
  auto make_reply = [&](const auto id) -> ipc_msg_t
  {
    if (success)
      return std::make_unique<kiq::okay_message>(g_platform, id);
    else
      return std::make_unique<kiq::fail_message>(g_platform, id);
  };

  if (pending_.empty())
  {
    kiq::log::klog().d("Received reply value, but not currently waiting to reply. Ignoring: {}", req.id);
    return;
  }

  ipc_msg_t msg;
  if (auto it = pending_.find(req.id); it != pending_.end())
  {
    if (success && req.info)
    {
      platform_info* data = static_cast<platform_info*>(it->second.get());
      msg                 = std::make_unique<platform_info>(data->platform(), req.text, data->type());
      pending_.erase(it);
    }
  }

  if (!msg)
    msg = make_reply(req.id);

  const auto&  payload   = msg->data();
  const size_t frame_num = payload.size();

  for (int i = 0; i < frame_num; i++)
  {
    auto flag = i == (frame_num - 1) ? zmq::send_flags::none : zmq::send_flags::sndmore;
    auto data = payload.at(i);

    zmq::message_t message{data.size()};
    std::memcpy(message.data(), data.data(), data.size());

    tx_.send(message, flag);
  }

  kiq::log::klog().t("Sent reply of {} as response to {}", constants::IPC_MESSAGE_NAMES.at(msg->type()), req.id);
}

void server::run()
{
  while (active_)
    recv();
}
//----------------------------------
void server::recv()
{
  using namespace kutils;
  using buffers_t = std::vector<ipc_message::byte_buffer>;

  auto is_duplicate = [this](const auto& m) { for (const auto& id : processed_) if (id == m->id()) return true; // match
                                              return false; };                                                  // no match
  zmq::message_t identity;
  if (!rx_.recv(identity) || identity.empty())
    return kiq::log::klog().e("Socket failed to receive");

  buffers_t      buffer;
  zmq::message_t msg;
  int            more_flag{1};

  while (more_flag && rx_.recv(msg))
  {
    more_flag = rx_.get(zmq::sockopt::rcvmore);
    buffer.push_back({static_cast<char*>(msg.data()), static_cast<char*>(msg.data()) + msg.size()});
  }

  ipc_msg_t   ipc_msg = DeserializeIPCMessage(std::move(buffer));
  kiq::log::klog().t("Message type is {}", constants::IPC_MESSAGE_NAMES.at(ipc_msg->type()));

  if (ipc_msg->type() == constants::IPC_PLATFORM_TYPE)
  {
    if (const auto decoded = static_cast<platform_message*>(ipc_msg.get()); is_duplicate(decoded))
    {
      kiq::log::klog().w("Ignoring duplicate IPC message");
      return;
    }
    else
      processed_.push_back(decoded->id());
  }

  msgs_.push_back(std::move(ipc_msg));
}
} // ns kiq::katrix
