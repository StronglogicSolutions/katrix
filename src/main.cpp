#include "katrix.hpp"

int main(int argc, char* argv[])
{
  std::string user = "someone";
  std::string pass = getpass("Password: ");

  katrix::KatrixBot bot{"someplace.com"};

  bot.login(user, pass);
  while (!bot.logged_in()) ;
  bot.run();

  return 0;
}