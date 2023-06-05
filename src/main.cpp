#include "katrix.hpp"

int main(int argc, char* argv[])
{
  kiq::katrix::klogger::init("katrix", "trace");
  auto log = kiq::katrix::klogger::instance();

  std::string server = "";
  std::string room   = "";
  std::string msg    = "";
  std::string path   = "";
  std::string user   = "";
  std::string pass   = "";

  kiq::katrix::KatrixBot bot{server, user, pass, room, [&](auto res, kiq::katrix::ResponseType type, kiq::katrix::RequestError e)
  {
    log.d(res);
    if (e)
      kiq::katrix::print_error(e);
    if (type == kiq::katrix::ResponseType::file_created)
      log.i("File created");
    else
    if (type == kiq::katrix::ResponseType::file_uploaded)
      log.i("File uploaded");
    else
    if (type == kiq::katrix::ResponseType::created)
      log.i("Message sent!");
  }};

  bot.login();
  while (!bot.logged_in()) ;
  bot.send_media_message(room, msg, {path});
  bot.run();

  return 0;
}