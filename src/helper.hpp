#include <iostream>
#include <unistd.h>
#include <fstream>
#include <sstream>
#include <kutils.hpp>
#include "mtx.hpp"
#include "mtxclient/http/client.hpp"
#include "mtxclient/http/errors.hpp"

namespace katrix {
std::shared_ptr<mtx::http::Client> g_client = nullptr;
using namespace mtx::client;
using namespace mtx::http;
using namespace mtx::events;

using PublicMessage = RoomEvent<msg::Text>;
///////////////////////////////////////////////////////////////
template<typename... Args>
static void log(Args... args) { for (const auto& s : { args... }) std::cout << s; std::cout << std::endl; }
///////////////////////////////////////////////////////////////
std::string get_sender(const mtx::events::collections::TimelineEvents &event)
{
    return std::visit([](auto e) { return e.sender; }, event);
}
///////////////////////////////////////////////////////////////
bool is_room_message(const mtx::events::collections::TimelineEvents &e)
{
    return (std::holds_alternative<mtx::events::RoomEvent<msg::Audio>>(e)) ||
           (std::holds_alternative<mtx::events::RoomEvent<msg::Emote>>(e)) ||
           (std::holds_alternative<mtx::events::RoomEvent<msg::File>>(e))  ||
           (std::holds_alternative<mtx::events::RoomEvent<msg::Image>>(e)) ||
           (std::holds_alternative<mtx::events::RoomEvent<msg::Notice>>(e))||
           (std::holds_alternative<mtx::events::RoomEvent<msg::Text>>(e))  ||
           (std::holds_alternative<mtx::events::RoomEvent<msg::Video>>(e));
}
///////////////////////////////////////////////////////////////
std::string get_body(const mtx::events::collections::TimelineEvents &e)
{
    if (auto ev = std::get_if<RoomEvent<msg::Audio>>(&e); ev != nullptr)
        return ev->content.body;
    else if (auto ev = std::get_if<RoomEvent<msg::Emote>>(&e); ev != nullptr)
        return ev->content.body;
    else if (auto ev = std::get_if<RoomEvent<msg::File>>(&e); ev != nullptr)
        return ev->content.body;
    else if (auto ev = std::get_if<RoomEvent<msg::Image>>(&e); ev != nullptr)
        return ev->content.body;
    else if (auto ev = std::get_if<RoomEvent<msg::Notice>>(&e); ev != nullptr)
        return ev->content.body;
    else if (auto ev = std::get_if<RoomEvent<msg::Text>>(&e); ev != nullptr)
        return ev->content.body;
    else if (auto ev = std::get_if<RoomEvent<msg::Video>>(&e); ev != nullptr)
        return ev->content.body;
    return "";
}
///////////////////////////////////////////////////////////////
void print_message(const mtx::events::collections::TimelineEvents &event)
{
  if (is_room_message(event)) log(get_sender(event), std::string{" : "}, get_body(event));
}
///////////////////////////////////////////////////////////////
static const auto IsMe = [](auto&& e) { return get_sender(e) == "@logicp:matrix.org"; };
///////////////////////////////////////////////////////////////
std::string error_to_string(RequestErr err)
{
  return
  "HTTP  code: " + std::to_string(err->status_code) + '\n' +
  "Error msg:  " + err->matrix_error.error + '\n' +
  "Error code: " + std::to_string(err->error_code);
}
///////////////////////////////////////////////////////////////
void print_error(RequestErr e) { std::cerr << error_to_string(e) << std::endl; }
///////////////////////////////////////////////////////////////
void login_handler(const mtx::responses::Login &res, RequestErr err)
{
  if (err)
  {
    log("There was an error during login: ", err->matrix_error.error.c_str());
    return;
  }

  log("Logged in as: ", res.user_id.to_string().c_str());

  g_client->set_access_token(res.access_token);
}
///////////////////////////////////////////////////////////////
void sync_handler(const mtx::responses::Sync &res, RequestErr err)
{
  auto callback = [](const mtx::responses::EventId &, RequestErr e) { if (e) error_to_string(e); };

  SyncOpts opts;

    if (err)
    {
      log("sync error:\n", error_to_string(err).c_str());
      opts.since = g_client->next_batch_token();
      g_client->sync(opts, &sync_handler);
      return;
    }

    for (const auto &room : res.rooms.join)
    {
      for (const auto &msg : room.second.timeline.events)
      {
        print_message(msg);
        if (room.first == "!BiClPQPHQPnqaRmuiV:matrix.org" && !IsMe(msg))
          g_client->send_room_message<mtx::events::msg::Text>(room.first, {"Automated message, bitch"}, callback);
      }
    }

    opts.since = res.next_batch;
    g_client->set_next_batch_token(res.next_batch);
    g_client->sync(opts, &sync_handler);
}
///////////////////////////////////////////////////////////////
void initial_sync_handler(const mtx::responses::Sync &res, RequestErr err)
{
  SyncOpts opts;

  if (err)
  {
    log("error during initial sync:\n", error_to_string(err).c_str());
    if (err->status_code != 200)
    {
      log("retrying initial sync ...");
      opts.timeout = 0;
      g_client->sync(opts, &initial_sync_handler);
    }
    return;
  }

  opts.since = res.next_batch;
  g_client->set_next_batch_token(res.next_batch);
  g_client->sync(opts, &sync_handler);
}

} // ns katrix
