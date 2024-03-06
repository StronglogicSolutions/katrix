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

  kiq::katrix::KatrixBot bot{server, user, pass, room};

  bot.login();
  while (!bot.logged_in()) ;
  bot.send_media_message(room, msg, {path});
  bot.run();

  return 0;
}