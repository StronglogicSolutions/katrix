#include "helper.hpp"

class KatrixBot
{
public:
KatrixBot(const std::string& server)
{
  g_client = std::make_shared<mtx::http::Client>(server);
}

template <typename T>
void send_message(const T& room_id, const T& msg, const std::vector<T>& media = {});
template <typename T>
void login(const T& username, const T& password)
{
  g_client->login(username, password, &login_handler);
}

void run()
{
  SyncOpts opts;
  opts.timeout = 0;
  g_client->sync(opts, &initial_sync_handler);
  g_client->close();
}

bool logged_in() const
{
  return g_client->access_token().size() > 0;
}

};
