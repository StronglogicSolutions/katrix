#include "katrix.hpp"

int main(int argc, char* argv[])
{
  std::string user = "logicp";
  std::string pass = getpass("Password: ");

  katrix::KatrixBot bot{"matrix.org"};

  bot.login(user, pass);
  while (!bot.logged_in()) ;
  bot.run();

  return 0;
}