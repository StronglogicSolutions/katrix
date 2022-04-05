#include "katrix.hpp"

int main(int argc, char* argv[])
{
  std::string server = "";
  std::string room   = "";
  std::string msg    = "";
  std::string path   = "";
  std::string user   = "";
  std::string pass   = ""; //getpass("Password: ");

  katrix::KatrixBot bot{server, user, pass, [&](auto res, katrix::ResponseType type, katrix::RequestError e)
  {
    katrix::log(res);
    if (e)
      katrix::log(katrix::error_to_string(e));
    if (type == katrix::ResponseType::file_created)
      katrix::log("File created");
    else
    if (type == katrix::ResponseType::file_uploaded)
      katrix::log("File uploaded");
    else
    if (type == katrix::ResponseType::created)
      katrix::log("Message sent!");
  }};

  bot.login();
  while (!bot.logged_in()) ;
  bot.send_media_message(room, msg, {path});
  bot.run();

  return 0;
}