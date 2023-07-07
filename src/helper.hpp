#include <condition_variable>
#include <mutex>
#include <chrono>
#include <iostream>
#include <unistd.h>
#include <fstream>
#include <sstream>
#include <kutils.hpp>
#include <logger.hpp>
#include "mtx.hpp"
#include "mtxclient/http/client.hpp"
#include "mtxclient/http/errors.hpp"

namespace kiq::katrix {
std::shared_ptr<mtx::http::Client> g_client = nullptr;
using namespace mtx::client;
using namespace mtx::http;
using namespace mtx::events;

using PublicMessage = RoomEvent<msg::Text>;
///////////////////////////////////////////////////////////////
using namespace kiq::log;
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
  if (is_room_message(event))
    klog().i("{} : {}", get_sender(event), get_body(event));
}
///////////////////////////////////////////////////////////////
static const auto IsMe = [](auto&& e) { return get_sender(e) == "@logicp:matrix.org"; };
///////////////////////////////////////////////////////////////
void print_error(RequestErr e)
{
  klog().e("HTTP  code: {}\nError msg:  {}\nError code: {}", e->status_code, e->matrix_error.error, e->error_code);
}
///////////////////////////////////////////////////////////////
void login_handler(const mtx::responses::Login &res, RequestErr err)
{
  if (err)
  {
    klog().e("There was an error during login: {}", err->matrix_error.error);
    return;
  }
  klog().i("{} logged in with device id {}", res.user_id.to_string(), res.device_id);

  g_client->set_access_token(res.access_token);
}
//----------------------------------------------------------------------------------------------
class bucket
{
using clock_t = std::chrono::steady_clock;
using duration_t = clock_t::duration;
static constexpr auto g_max = clock_t::duration{std::chrono::minutes(1)};
//------------------------------------
public:
  bool request(int quantity)
  {
    refill();

    const auto required = (quantity * (rate_));
    const auto result   = required <= available_;

    if (result)
      available_ -= required;

    klog().d("Bucket request result: {}", result);
    return result;
  }

  bool has_token() const
  {
    const auto result = (available_ >= rate_);
    klog().d("Bucket has tokens: {}", result);
    return result;
  }
//------------------------------------
private:
  void refill()
  {
    const auto now  = clock_t::now();
    available_     += now - last_refill_;
    available_      = std::min(available_, g_max);
    last_refill_    = now;
  }
//------------------------------------
  clock_t::duration   available_  {g_max};
  clock_t::duration   rate_       {clock_t::duration(std::chrono::minutes(1)) / 4};
  clock_t::time_point last_refill_{clock_t::now()};
};
//-------------------------------------
static std::string to_json(const mtx::events::presence::Presence p, const std::string& name = "")
{
  return std::string{
      "{\"name\": \"" + name +
    "\",\"avatar\":\"" + p.avatar_url +
    "\",\"last\": \"" + std::to_string(p.last_active_ago) +
    "\",\"active\": \"" + std::to_string(p.currently_active) +
    "\",\"status\": \"" + p.status_msg +
    "\",\"presence\": \"" + mtx::presence::to_string(p.presence) + "\"}"};
}
} // ns kiq::katrix
