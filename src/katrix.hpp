#pragma once

#include "helper.hpp"
#include <deque>
#include <csignal>

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
using Msg_t            = mtx::events::msg::Text;
using Image_t          = mtx::events::msg::Image;
using Video_t          = mtx::events::msg::Video;
using EventID          = mtx::responses::EventId;
using Groups           = mtx::responses::JoinedGroups;
using RequestError     = mtx::http::RequestErr;

template <typename T>
auto get_response_type = []
{
  if constexpr (std::is_same_v<T, Msg_t>)   return ResponseType::created;
  if constexpr (std::is_same_v<T, Video_t>) return ResponseType::file_uploaded;
  if constexpr (std::is_same_v<T, Image_t>) return ResponseType::file_uploaded;
                                            return ResponseType::unknown;
};

struct TXMessage
{
  using MimeType = kutils::MimeType;
  struct File
  {
    File(const std::string& name)
    : filename(name),
      mime(kutils::GetMimeType(name))
    {
    }

    std::string filename;
    std::string mtx_url;
    MimeType    mime;

    bool ready() const { return !mtx_url.empty(); }
  };

  using Files_t = std::vector<TXMessage::File>;
  TXMessage(const std::string& msg, const std::string& id, const std::vector<std::string>& urls)
  : message(msg),
    room_id(id),
    mx_count(urls.size()),
    files([](auto paths) { Files_t f{}; for (const auto& p : paths) f.emplace_back(File{p}); return f; }(urls))
  {}

  std::string message;
  std::string room_id;
  size_t      mx_count;
  Files_t     files;
};

template <typename T>
static T get_file_type(TXMessage::File file)
{
  if constexpr (std::is_same_v<T, Image_t>)
    return Image_t{"Katrix Image", "m.image", file.mtx_url, mtx::common::ImageInfo{
      0, 0, 0, mtx::common::ThumbnailInfo{0, 0, 0, file.mime.name}, file.mtx_url, file.mime.name
    }};
  if constexpr (std::is_same_v<T, Video_t>)
    return Video_t{"Katrix Video", "m.video", file.mtx_url, mtx::common::VideoInfo{
      0, 0, 0, 0, file.mime.name, ""/*thumbURL*/, mtx::common::ThumbnailInfo{0, 0, 0, ""/*thumbMIME*/}
    }};
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

void send_media_message(const std::string& room_id, const std::string& msg, const std::vector<std::string>& paths)
{
  m_tx_queue.push_back(TXMessage{msg, room_id, paths});
  auto callback = [this, &room_id](mtx::responses::ContentURI uri, RequestError e)
  {
    if (e) print_error(e);
    else
    {
      for (auto it = m_tx_queue.begin(); it != m_tx_queue.end();)
      {
        for (TXMessage::File& file : it->files)
        {
          if (!file.ready())
          {
            file.mtx_url = uri.content_uri;
            it->mx_count--;
          }
        }
        if (!(it->mx_count))
        {
          send_media  (it->room_id, it->files);
          send_message(it->room_id, {it->message});
          it = m_tx_queue.erase(it);
        }
        else
          it++;
      }
    }
  };
  for (const auto& path : paths)
    upload(path, callback);
}

template <typename T = TXMessage::Files_t>
void send_media(const std::string& id, T&& files)
{
  auto get_files_t = [](auto&& v)
  {
    TXMessage::Files_t f; for (auto&& m : v) f.emplace_back(TXMessage::File{m});
    return f;
  };

  if constexpr (std::is_same_v<std::decay_t<T>, TXMessage::Files_t>)
    for (const auto& file : files)
      if (file.mime.IsPhoto())
        send_message<Image_t>(id, get_file_type<Image_t>(file));
      else
        send_message<Video_t>(id, get_file_type<Video_t>(file));
  if constexpr (std::is_same_v<std::decay_t<T>, std::vector<std::string>>)
    for (const auto& file : get_files_t(files))
      if (file.mime.IsPhoto())
        send_message<Image_t>(id, get_file_type<Image_t>(file));
      else
        send_message<Video_t>(id, get_file_type<Video_t>(file));
}

template <typename T = Msg_t>
void send_message(const std::string& room_id, const T& msg, const std::vector<std::string>& media = {})
{
  auto callback = [this](EventID res, RequestError e)
  {
    if (e)               print_error(e);
    if (use_callback())  m_cb(res.event_id.to_string(), get_response_type<T>(), e);
  };

  send_media(room_id, media);
  g_client->send_room_message<T>(room_id, {msg}, callback);
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

using UploadCallback = std::function<void(mtx::responses::ContentURI, RequestError)>;
void upload(const std::string& path, UploadCallback cb = nullptr)
{
  auto callback = (cb) ? cb : [this](mtx::responses::ContentURI uri, RequestError e)
  {
    if (e) print_error(e);
    if (use_callback()) m_cb(uri.content_uri, ResponseType::file_created, e);
  };
  auto bytes = kutils::ReadFile(path);
  auto pos   = path.find_last_of("/");
  std::string filename = (pos == std::string::npos) ? path : path.substr(pos + 1);

  g_client->upload(bytes, "application/octet-stream", filename, callback);
}

void run(bool error = false)
{
  try
  {
    if (error)
    {
      login();
      while (!logged_in()) ;
    }

    SyncOpts opts;
    opts.timeout = 0;
    g_client->sync(opts, &initial_sync_handler);
    g_client->close();
  }
  catch(const std::exception& e)
  {
    std::cerr << "Exception caught: " << e.what() << std::endl;
    if (!error)
      run(true);
    else
      throw;
  }
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

CallbackFunction      m_cb;
std::string           m_username;
std::string           m_password;
std::deque<TXMessage> m_tx_queue;
};
} // ns katrix
