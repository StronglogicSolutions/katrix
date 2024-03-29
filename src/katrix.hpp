#pragma once

#include <unordered_set>
#include "helper.hpp"
#include "server.hpp"
#include <csignal>
#include <nlohmann/json.hpp>

namespace kiq::katrix {

struct poll
{
  using answer_pair_t = std::pair<std::string, std::vector<std::string>>;
  using names_t       = std::vector<std::string>;
  using answers_t     = std::vector<std::string>;
  using result_t      = std::map<std::string, std::vector<std::string>>;
  //--------------------------------------
  std::string question;
  result_t    results;
  //--------------------------------------
  result_t get() const
  {
    return results;
  }
  //--------------------------------------
  void set(std::string_view q, answers_t answers)
  {
    question = q;
    for (const auto& answer : answers)
      results.insert_or_assign(answer, names_t{});
  }
  //--------------------------------------
  void vote(std::string_view answer, std::string_view name)
  {
    if (results.find(answer.data()) == results.end())
      return;
    results[answer.data()].emplace_back(name.data());
  }
  //--------------------------------------
  std::string ask() const
  {
    std::string q = question + '\n';
    for (const auto& [a, _] : results)
      q += a + '\n';
    return q;
  }
};
//------------------------------------------------
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
using rooms_t          = std::map<std::string, std::vector<std::string>>;
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
          const std::string& room = "")
: m_username(user),
  m_password(pass),
  m_room_id (room)
{
  g_client = std::make_shared<mtx::http::Client>(server);
}
//------------------------------------------------
void send_media_message(const std::string& room_id, const std::string& msg, const std::vector<std::string>& paths, CallbackFunction on_finish = nullptr)
{
  m_uploading = (!paths.empty());
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
          m_uploading = false;
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
  klog().t("send_media() called with {} files", files.size());
  for (const auto& file : files)
    if (file.mime.IsPhoto())
      send_message<Image_t>(id, get_file_type<Image_t>(file));
    else
      send_message<Video_t>(id, get_file_type<Video_t>(file));
}
//------------------------------------------------
template <typename T = Msg_t>
void send_message(const std::string& room_id, const T& msg, const std::vector<std::string>& media = {}, CallbackFunction cb = nullptr)
{
  klog().i("Sending message to {}", room_id);
  auto callback = [this, cb = std::move(cb)](EventID res, RequestError e)
  {
    klog().d("Message send callback event: {}", res.event_id.to_string());
    if (e)
      print_error(e);
    if (cb)
      cb(res.event_id.to_string(), get_response_type<T>(), e);
  };

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
void upload(const std::string& path, UploadCallback cb)
{
  auto get_clean_path = [&path]
  {
    const auto pos = path.find("://");
    return (pos != std::string::npos) ? path.substr(pos + 3) : path;
  };

  klog().d("Uploading file with path {}", path);

  const auto bytes    = kutils::ReadFile(get_clean_path());
  const auto pos      = path.find_last_of("/");
  const auto filename = (pos == std::string::npos) ? path : path.substr(pos + 1);

  g_client->upload(bytes, "application/octet-stream", filename, cb);
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
void get_user_info(CallbackFunction cb)
{
  klog().i("Getting user info for {}", m_username);
  auto callback = [this, cb = std::move(cb)](mtx::events::presence::Presence res, RequestError e)
  {
    std::string data;

    if (e)
      print_error(e);
    else
      data = to_json(res, m_username);

    cb(data, ResponseType::user_info, e);
  };

  g_client->presence_status("@" + m_username + ":" + g_client->server(), callback);
}
//------------------------------------------------
void fetch_rooms()
{
  for (const auto& [id, aliases] : m_rooms)
    if (aliases.empty())
      g_client->list_room_aliases(id, [this, id](auto res, auto err) { m_rooms[id] = res.aliases; });
}
//------------------------------------------------
rooms_t
get_rooms(CallbackFunction callback) const
{
  if (callback)
  {
    std::string rooms;
    for (const auto& [id, aliases] : m_rooms)
      if (!aliases.empty())
        rooms += id + ',' + aliases.front() + '\n';
    callback(rooms, ResponseType::rooms, {});
  }

  return m_rooms;
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
      if (!m_rooms.contains(room.first))
        m_rooms[room.first] = {};
      for (const auto &msg : room.second.timeline.events)
        print_message(msg);
    }

    opts.since = res.next_batch;
    g_client->set_next_batch_token(res.next_batch);
    g_client->sync(opts, [this](const auto& resp, const auto& err) { sync_handler(resp, err); });

    process_channel();
    process_queue();
    fetch_rooms();
}
//------------------------------------------------
void process_channel()
{
  while (m_server.has_msgs())
   {
     klog().d("Processing server message");
     process_request(m_converter.receive(std::move(m_server.get_msg())));
   }
}
//------------------------------------------------
void process_request(const request_t& req)
{
  auto callback = [this, req](auto resp, auto type, auto err)
  {
    request_t out = req;
    if (req.info)
      out.text = resp;
    klog().t("Request callback invoked with id {} and text {}", out.id, out.text);
    m_server.reply(out, !err);
  };

  klog().t("Processing request");
  if (req.info)
  {
    klog().d("Info request of type {}", req.text);
    if (req.text == "matrix:info")
      get_user_info(callback);
    else
    if (req.text == "matrix:rooms")
      get_rooms(callback);
    else
      klog().w("Failed to handle info request");
    return;
  }

  m_queue.push_back([this, rx = std::move(req), cb = std::move(callback)]
  {
    if (rx.media.empty())
    {
      klog().i("Sending \"{}\" msg to {}", rx.text, m_room_id);
      return send_message(m_room_id, Msg_t{rx.text}, {}, cb);
    }

    send_media_message(m_room_id, {rx.text}, { kutils::urls_from_string(rx.media).front() }, cb); // Only send one file
  });
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

//------------------------------------------------
void set_poll(const std::string& question, const std::vector<std::string>& answers)
{
  // Timer by time?
  // JSON Store
  // Generate Image of POLL?
  m_poll.set(question, answers);
}
//________________________________________________
void vote(const std::string& answer, const std::string& name)
{
  m_poll.vote(answer, name);
}
//------------------------------------------------
//------------------------------------------------
private:
//------------------------------------------------
void process_queue()
{
  while (!m_uploading && !m_queue.empty())
  {
    if (!m_tokens.request(1))
      return;

    const auto fn = m_queue.front();
    fn();
    m_queue.pop_front();
  }
}
//------------------------------------------------
using queue_t = std::deque<std::function<void()>>;

std::string           m_username;
std::string           m_password;
std::string           m_room_id;
std::deque<TXMessage> m_tx_queue;
server                m_server;
request_converter     m_converter;
bucket                m_tokens;
queue_t               m_queue;
poll                  m_poll;
rooms_t               m_rooms;
bool                  m_uploading{false};
};
} // ns kiq::katrix
