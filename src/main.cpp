#include "katrix.hpp"

int main(int argc, char* argv[])
{
  katrix::klogger::init("katrix", "trace");
  auto log = katrix::klogger::instance();

  std::string server = "";
  std::string room   = "";
  std::string msg    = "";
  std::string path   = "";
  std::string user   = "";
  std::string pass   = "";

  katrix::KatrixBot bot{server, user, pass, [&](auto res, katrix::ResponseType type, katrix::RequestError e)
  {
    log.d(res);
    if (e)
      katrix::print_error(e);
    if (type == katrix::ResponseType::file_created)
      log.i("File created");
    else
    if (type == katrix::ResponseType::file_uploaded)
      log.i("File uploaded");
    else
    if (type == katrix::ResponseType::created)
      log.i("Message sent!");
  }};

  bot.login();
  while (!bot.logged_in()) ;
  bot.send_media_message(room, msg, {path});
  bot.run();

  return 0;
}