#include "helper.hpp"
namespace katrix {
enum class ResponseType
{
  created,
  rooms,
  user_info,
  file_created,
  file_uploaded,
  unknown
};

using CallbackFunction = std::function<void(std::string, ResponseType, RequestErr)>;
using MessageType      = mtx::events::msg::Text;
using FileType         = mtx::events::msg::File;
using EventID          = mtx::responses::EventId;
using Groups           = mtx::responses::JoinedGroups;
using RequestError     = mtx::http::RequestErr;

template <typename T>
auto get_response_type = []()
{
  if constexpr (std::is_same_v<T, MessageType>)
    return ResponseType::created;
  if constexpr (std::is_same_v<T, FileType>)
    return ResponseType::file_uploaded;
  return ResponseType::unknown;
};

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
  auto callback = [this](EventID res, RequestError e)
  {
    if (e)               print_error(e);
    if (use_callback())  m_cb(res.event_id.to_string(), get_response_type<S>(), e);
  };

  for (const auto& file : media)
    send_message<mtx::events::msg::File>(room_id, mtx::events::msg::File{"file", file});
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

void upload(std::string path)
{
  auto callback = [this](mtx::responses::ContentURI uri, RequestError e)
  {
    if (e) print_error(e);
    if (use_callback()) m_cb(uri.content_uri, ResponseType::file_created, e);
  };
  auto bytes = read_file(path);
  auto pos   = path.find_last_of("/");
  std::string filename = (pos == std::string::npos) ? path : path.substr(pos + 1);

  g_client->upload(bytes, "application/octet-stream", filename, callback);
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

void get_user_info()
{
  auto callback = [this](mtx::events::presence::Presence res, RequestError e)
  {
    auto Parse = [](auto res)
    {
      return "Name: "        + res.displayname                      + "\n" +
             "Last active: " + std::to_string(res.last_active_ago)  + "\n" +
             "Online: "      + std::to_string(res.currently_active) + "\n" +
             "Status: "      + res.status_msg;
    };
    if (e)              print_error(e);
    if (use_callback()) m_cb(Parse(res), ResponseType::user_info, e);
  };

  g_client->presence_status("@" + m_username + ":" + g_client->server(), callback);
}

void get_rooms()
{
  if (use_callback()) m_cb("12345,room1\n67890,room2", ResponseType::rooms, RequestError{});
}

private:

bool use_callback() const
{
  return nullptr != m_cb;
}

template <typename T>
void callback(T res, RequestErr e)
{
  if constexpr (std::is_same_v<T, mtx::responses::JoinedGroups>)
  {
    if (e)               print_error(e);
    if (use_callback())  m_cb(res, e);
  }
  else
  if constexpr (std::is_same_v<T, const mtx::responses::EventId>)
  {
    if (e)               print_error(e);
    if (use_callback())  m_cb(res, e);
  }
}

CallbackFunction m_cb;
std::string      m_username;
std::string      m_password;
};
} // ns katrix
