#include "helper.hpp"
namespace katrix {
using CallbackFunction = std::function<void(const mtx::responses::EventId&, RequestErr)>;
using MessageType      = mtx::events::msg::Text;
using EventID          = mtx::responses::EventId;
using RequestError     = mtx::http::RequestErr;
class KatrixBot
{
public:
KatrixBot(const std::string& server,
          const std::string& user = "",
          const std::string& pass = "",
          CallbackFunction   cb   = nullptr)
: m_username(user),
  m_password(pass),
  m_cb      (cb)
{
  g_client = std::make_shared<mtx::http::Client>(server);
}

template <typename T, typename S = MessageType>
void send_message(const T& room_id, const S& msg, const std::vector<T>& media = {})
{
  auto callback = [this](const mtx::responses::EventId& id, RequestErr e)
  {
    if (e)               print_error(e);
    if (use_callback())  m_cb(id, e);
  };
  g_client->send_room_message<S>(room_id, {msg}, callback);
}
template <typename T = std::string>
void login(const T& username = "", const T& password = "")
{
  if (!username.empty())
  {
    m_username = username;
    m_password = password;
  }

  g_client->login(m_username, m_password, &login_handler);
}

void run()
{
  SyncOpts opts;
  opts.timeout = 0;
  g_client->sync(opts, &initial_sync_handler);
  g_client->close();
}

bool logged_in() const
{
  return g_client->access_token().size() > 0;
}

private:

bool use_callback() const
{
  return nullptr != m_cb;
}

CallbackFunction m_cb;
std::string      m_username;
std::string      m_password;
};
} // ns katrix
