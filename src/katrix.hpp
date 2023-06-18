#pragma once

#include "helper.hpp"
#include "server.hpp"
#include <csignal>

namespace kiq::katrix {
enum class ResponseType
{
  created,
  rooms,
  user_info,
  file_created,
  file_uploaded,
  unknown
};
//------------------------------------------------
using ReqErr           = mtx::http::RequestErr;
using CallbackFunction = std::function<void(std::string, ResponseType, ReqErr)>;
using Msg_t            = mtx::events::msg::Text;
using Image_t          = mtx::events::msg::Image;
using Video_t          = mtx::events::msg::Video;
using EventID          = mtx::responses::EventId;
using RequestError     = mtx::http::RequestErr;
//------------------------------------------------
template <typename T>
auto get_response_type = []
{
  if constexpr (std::is_same_v<T, Msg_t>)   return ResponseType::created;
  if constexpr (std::is_same_v<T, Video_t>) return ResponseType::file_uploaded;
  if constexpr (std::is_same_v<T, Image_t>) return ResponseType::file_uploaded;
                                            return ResponseType::unknown;
};
//------------------------------------------------
//------------------------------------------------
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
//------------------------------------------------
  using Files_t = std::vector<TXMessage::File>;
  TXMessage(const std::string& msg, const std::string& id, const std::vector<std::string>& urls)
  : message(msg),
    room_id(id),
    mx_count(urls.size()),
    files([](auto paths) { Files_t f{}; for (const auto& p : paths) f.emplace_back(File{p}); return f; }(urls))
  {}
//------------------------------------------------
  std::string message;
  std::string room_id;
  size_t      mx_count;
  Files_t     files;
};
//------------------------------------------------
//------------------------------------------------
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
//------------------------------------------------
//------------------------------------------------
class KatrixBot
{
public:
KatrixBot(const std::string& server,
          const std::string& user = "",
          const std::string& pass = "",
          const std::string& room = "",
          CallbackFunction   cb   = nullptr)
: m_cb      (cb),
  m_username(user),
  m_password(pass),
  m_room_id (room)
{
  g_client = std::make_shared<mtx::http::Client>(server);
}
//------------------------------------------------
void send_media_message(const std::string& room_id, const std::string& msg, const std::vector<std::string>& paths, CallbackFunction on_finish = nullptr)
{
  klog().d("Sending media message with {} urls", paths.size());
  m_tx_queue.push_back(TXMessage{msg, room_id, paths});
  auto callback = [this, &room_id, on_finish = std::move(on_finish)](mtx::responses::ContentURI uri, RequestError e)
  {
    klog().d("Media message callback received uri: {}", uri.content_uri);
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
          klog().t("Sending another media message");
          send_media  (it->room_id, it->files);
          send_message(it->room_id, {it->message}, {}, on_finish);
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
//------------------------------------------------
template <typename T = TXMessage::Files_t>
void send_media(const std::string& id, T&& files)
{
  auto get_files_t = [](auto&& v)
  {
    TXMessage::Files_t f; for (auto&& m : v) f.emplace_back(TXMessage::File{m});
    return f;
  };

  if constexpr (std::is_same_v<std::decay_t<T>, TXMessage::Files_t>)
  {
    klog().t("send_media() called with {} files", files.size());
    for (const auto& file : files)
      if (file.mime.IsPhoto())
        send_message<Image_t>(id, get_file_type<Image_t>(file));
      else
        send_message<Video_t>(id, get_file_type<Video_t>(file));
  }
  if constexpr (std::is_same_v<std::decay_t<T>, std::vector<std::string>>)
  {
    klog().t("send_media() called with {} strings", files.size());
    for (const auto& file : get_files_t(files))
      if (file.mime.IsPhoto())
        send_message<Image_t>(id, get_file_type<Image_t>(file));
      else
        send_message<Video_t>(id, get_file_type<Video_t>(file));
  }
}
//------------------------------------------------
template <typename T = Msg_t>
void send_message(const std::string& room_id, const T& msg, const std::vector<std::string>& media = {}, CallbackFunction on_finish = nullptr)
{
  klog().i("Sending message to {}", room_id);
  auto callback = [this, on_finish = std::move(on_finish)](EventID res, RequestError e)
  {
    klog().d("Message send callback event: {}", res.event_id.to_string());
    if (e)               print_error(e);
    if (use_callback())  m_cb     (res.event_id.to_string(), get_response_type<T>(), e);
    if (on_finish)       on_finish(res.event_id.to_string(), get_response_type<T>(), e);
  };

  send_media(room_id, media); // TODO: Safely remove
  g_client->send_room_message<T>(room_id, {msg}, callback);
}
//------------------------------------------------
template <typename T = std::string>
void login(const T& username = "", const T& password = "")
{
  klog().i("{} is logging in", username);
  if (!username.empty())
  {
    m_username = username;
    m_password = password;
  }

  g_client->login(m_username, m_password, &login_handler);
}
//------------------------------------------------
using UploadCallback = std::function<void(mtx::responses::ContentURI, RequestError)>;
void upload(const std::string& path, UploadCallback cb = nullptr)
{
  klog().d("Uploading file with path {}", path);
  auto callback = (cb) ? cb : [this](mtx::responses::ContentURI uri, RequestError e)
  {
    if (e) print_error(e);
    klog().d("Received file URI {}:", uri.content_uri);
    if (use_callback()) m_cb(uri.content_uri, ResponseType::file_created, e);
  };
  auto bytes = kutils::ReadFile(path);
  auto pos   = path.find_last_of("/");
  std::string filename = (pos == std::string::npos) ? path : path.substr(pos + 1);

  g_client->upload(bytes, "application/octet-stream", filename, callback);
}
//------------------------------------------------
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
    g_client->sync(opts, [this](const auto& resp, const auto& err) { initial_sync_handler(resp, err); });
    g_client->close();
  }
  catch(const std::exception& e)
  {
    klog().e("Exception caught: {}", e.what());

    if (!error)
      run(true);
    else
      throw;
  }
}
//------------------------------------------------
bool logged_in() const
{
  return g_client->access_token().size() > 0;
}
//------------------------------------------------
void get_user_info()
{
  klog().i("Getting user info for {}", m_username);
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
//------------------------------------------------
void get_rooms()
{
  if (use_callback()) m_cb("12345,room1\n67890,room2", ResponseType::rooms, RequestError{});
}
//------------------------------------------------
void sync_handler(const mtx::responses::Sync &res, RequestErr err)
{
  auto callback = [](const mtx::responses::EventId &, RequestErr e) { if (e) print_error(e); };
  SyncOpts opts;

    if (err)
    {
      klog().e("Sync error");
      print_error(err);
      opts.since = g_client->next_batch_token();
      g_client->sync(opts, [this] (const auto& resp, const auto& err) { sync_handler(resp, err); });
      return;
    }

    for (const auto &room : res.rooms.join)
    {
      for (const auto &msg : room.second.timeline.events)
      {
        print_message(msg);
        if (room.first == m_room_id && !IsMe(msg))
          g_client->send_room_message<mtx::events::msg::Text>(room.first, {"Automated message, bitch"}, callback);
      }
    }

    opts.since = res.next_batch;
    g_client->set_next_batch_token(res.next_batch);
    g_client->sync(opts, [this](const auto& resp, const auto& err) { sync_handler(resp, err); });

    while (m_server.has_msgs())
    {
      klog().d("Processing server message");
      process_request(m_converter.receive(std::move(m_server.get_msg())));
    }

}
//------------------------------------------------
void process_request(const request_t& req)
{
  klog().t("Processing request");
  if (req.info)
  {
    get_user_info();
    return;
  }
  if (req.media.empty())
  {
    klog().i("Sending \"{}\" msg to {}", req.text, m_room_id);
    send_message(m_room_id, Msg_t{req.text}, {}, [this, &req](auto resp, auto type, auto err)
    {
      m_server.reply(req, !err);
    });
    return;
  }

  send_media_message(m_room_id, {req.text}, kutils::urls_from_string(req.media));
}
//------------------------------------------------
void initial_sync_handler(const mtx::responses::Sync &res, RequestErr err)
{
  SyncOpts opts;

  if (err)
  {
    klog().e("error during initial sync");
    print_error(err);
    if (err->status_code != 200)
    {
      klog().w("retrying initial sync ...");
      opts.timeout = 0;
      g_client->sync(opts, [this](const auto& resp, const auto& err) { initial_sync_handler(resp, err); });
    }
    return;
  }

  opts.since = res.next_batch;
  g_client->set_next_batch_token(res.next_batch);
  g_client->sync(opts, [this](const auto& resp, const auto& err) { sync_handler(resp, err); });
}

private:

bool use_callback() const
{
  return nullptr != m_cb;
}
//------------------------------------------------
template <typename T>
void callback(T res, RequestErr e)
{
  if constexpr (std::is_same_v<T, const mtx::responses::EventId>)
  {
    if (e)               print_error(e);
    if (use_callback())  m_cb(res, e);
  }
  else
    klog().w("Callback received unknown response");
}
//------------------------------------------------
CallbackFunction      m_cb;
std::string           m_username;
std::string           m_password;
std::string           m_room_id;
std::deque<TXMessage> m_tx_queue;
server                m_server;
request_converter     m_converter;
};
} // ns kiq::katrix
