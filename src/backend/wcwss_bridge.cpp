// Linux transport for the original wetap network layer. This file must be
// compiled with clang + libc++ because its boundary contains libc++ objects.
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <curl/curl.h>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {
using StringMap = std::map<std::string, std::string>;
struct OpaqueConfig {
  unsigned char storage[1];
};
struct OpaqueProfile {
  alignas(16) unsigned char storage[512]{};
};

struct Connection {
  std::shared_ptr<void> callback;
  std::string client, url, reason;
  StringMap headers;
  std::vector<std::string> protocols;
  uint32_t id = 0;
  std::atomic<bool> running{true};
  std::mutex curlMutex;
  CURL *curl = nullptr;
  int closeCode = 1000;
};

std::mutex connectionsMutex;
std::map<std::pair<std::string, uint32_t>, std::shared_ptr<Connection>>
    connections;
std::atomic<uint32_t> nextId{1};
std::atomic<unsigned> bridgeCalls{0};
std::atomic<int> bridgeState{0}, bridgeError{-1};
std::atomic<unsigned> bridgeSends{0}, bridgeReceives{0};
std::once_flag curlInitialization;
CURLcode curlInitializationResult = CURLE_FAILED_INIT;
void initializeCurl() {
  std::call_once(curlInitialization, [] {
    curlInitializationResult = curl_global_init(CURL_GLOBAL_DEFAULT);
  });
}
bool supportsProtocol(const char *name) {
  initializeCurl();
  const auto *version = curl_version_info(CURLVERSION_NOW);
  for (auto protocol = version->protocols; protocol && *protocol; ++protocol)
    if (!std::strcmp(*protocol, name))
      return true;
  return false;
}

template <class Function>
Function callback(const std::shared_ptr<void> &owner, size_t index) {
  if (!owner)
    return nullptr;
  auto object = owner.get();
  return reinterpret_cast<Function>(
      (*reinterpret_cast<void ***>(object))[index]);
}
void onOpen(const std::shared_ptr<Connection> &connection, bool success,
            int error, const std::string &message) {
  bridgeState = success ? 1 : 2;
  bridgeError = error;
  using Function =
      void (*)(void *, const std::string &, uint32_t, bool, const StringMap &,
               int, const std::string &, const OpaqueProfile &);
  OpaqueProfile profile;
  if (auto function = callback<Function>(connection->callback, 2))
    function(connection->callback.get(), connection->client, connection->id,
             success, connection->headers, error, message, profile);
}
void onMessage(const std::shared_ptr<Connection> &connection,
               const std::vector<unsigned char> &message, bool binary) {
  ++bridgeReceives;
  using Function = void (*)(void *, const std::string &, uint32_t, const char *,
                            size_t, bool);
  if (auto function = callback<Function>(connection->callback, 3))
    function(connection->callback.get(), connection->client, connection->id,
             reinterpret_cast<const char *>(message.data()), message.size(),
             binary);
}
void onClose(const std::shared_ptr<Connection> &connection, int code,
             const std::string &message) {
  using Function =
      void (*)(void *, const std::string &, uint32_t, int, const std::string &);
  if (auto function = callback<Function>(connection->callback, 4))
    function(connection->callback.get(), connection->client, connection->id,
             code, message);
}
void erase(const std::shared_ptr<Connection> &connection) {
  std::lock_guard lock(connectionsMutex);
  connections.erase({connection->client, connection->id});
}
void run(const std::shared_ptr<Connection> &connection) {
  initializeCurl();
  if (curlInitializationResult != CURLE_OK || !supportsProtocol("ws") ||
      !supportsProtocol("wss")) {
    onOpen(connection, false, CURLE_UNSUPPORTED_PROTOCOL,
           "libcurl lacks WS/WSS support; use the bundled transport");
    erase(connection);
    return;
  }
  CURL *curl = curl_easy_init();
  if (!curl) {
    onOpen(connection, false, CURLE_FAILED_INIT, "curl initialization failed");
    erase(connection);
    return;
  }
  curl_slist *headers = nullptr;
  for (const auto &[name, value] : connection->headers) {
    auto line = name + ": " + value;
    headers = curl_slist_append(headers, line.c_str());
  }
  if (!connection->protocols.empty()) {
    std::string value;
    for (const auto &protocol : connection->protocols) {
      if (!value.empty())
        value += ", ";
      value += protocol;
    }
    headers = curl_slist_append(headers,
                                ("Sec-WebSocket-Protocol: " + value).c_str());
  }
  curl_easy_setopt(curl, CURLOPT_URL, connection->url.c_str());
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  curl_easy_setopt(curl, CURLOPT_CONNECT_ONLY, 2L);
  curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);
  curl_easy_setopt(curl, CURLOPT_HAPPY_EYEBALLS_TIMEOUT_MS, 200L);
  curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
  curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
  curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 8000L);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 12000L);
  curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
  const auto started = std::chrono::steady_clock::now();
  CURLcode result = curl_easy_perform(curl);
  const bool connectionFailure =
      result == CURLE_COULDNT_CONNECT || result == CURLE_OPERATION_TIMEDOUT ||
      result == CURLE_SSL_CONNECT_ERROR || result == CURLE_RECV_ERROR ||
      result == CURLE_GOT_NOTHING;
  const long remaining =
      12000L - std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::steady_clock::now() - started)
                   .count();
  // Happy Eyeballs races TCP connections. A selected IPv6 endpoint can still
  // fail during TLS or upgrade, so retry A records within the same deadline.
  if (connectionFailure && connection->running && remaining > 0) {
    if (getenv("WETYPE_DEBUG_NETWORK"))
      fprintf(stderr, "WSS_BRIDGE retry=ipv4 first_error=%d\n", int(result));
    curl_easy_setopt(curl, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4);
    curl_easy_setopt(curl, CURLOPT_FRESH_CONNECT, 1L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS,
                     std::min(8000L, remaining));
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, remaining);
    result = curl_easy_perform(curl);
  }
  if (result != CURLE_OK) {
    onOpen(connection, false, result, curl_easy_strerror(result));
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    erase(connection);
    return;
  }
  {
    std::lock_guard lock(connection->curlMutex);
    connection->curl = curl;
  }
  onOpen(connection, true, 0, {});
  std::vector<unsigned char> message;
  std::vector<unsigned char> closeMessage;
  bool binary = false;
  while (connection->running) {
    unsigned char buffer[16384];
    size_t received = 0;
    const curl_ws_frame *frame = nullptr;
    curl_ws_frame metadata{};
    {
      std::lock_guard lock(connection->curlMutex);
      result = curl_ws_recv(curl, buffer, sizeof(buffer), &received, &frame);
      // libcurl invalidates this pointer at the next WebSocket call,
      // including a concurrent send. Copy it before releasing the lock.
      if (result == CURLE_OK && frame)
        metadata = *frame;
    }
    if (result == CURLE_AGAIN) {
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
      continue;
    }
    if (result != CURLE_OK) {
      std::lock_guard lock(connection->curlMutex);
      connection->closeCode = result;
      connection->reason = curl_easy_strerror(result);
      break;
    }
    if (!frame)
      continue;
    if (metadata.flags & (CURLWS_PING | CURLWS_PONG))
      continue;
    if (metadata.flags & CURLWS_CLOSE) {
      closeMessage.insert(closeMessage.end(), buffer, buffer + received);
      if (metadata.bytesleft != 0)
        continue;
      std::lock_guard lock(connection->curlMutex);
      connection->closeCode =
          closeMessage.size() >= 2
              ? (int(closeMessage[0]) << 8) | closeMessage[1]
              : 1005;
      connection->reason =
          closeMessage.size() > 2
              ? std::string(closeMessage.begin() + 2, closeMessage.end())
              : std::string();
      break;
    }
    if (!(metadata.flags & (CURLWS_TEXT | CURLWS_BINARY | CURLWS_CONT)))
      continue;
    if (message.empty())
      binary = (metadata.flags & CURLWS_BINARY) != 0;
    if (message.size() + received > 16777216) {
      std::lock_guard lock(connection->curlMutex);
      connection->closeCode = 1009;
      connection->reason = "WebSocket message too large";
      break;
    }
    message.insert(message.end(), buffer, buffer + received);
    if (metadata.bytesleft == 0 && !(metadata.flags & CURLWS_CONT)) {
      onMessage(connection, message, binary);
      message.clear();
    }
  }
  curl_slist_free_all(headers);
  int closeCode;
  std::string reason;
  {
    std::lock_guard lock(connection->curlMutex);
    curl_easy_cleanup(curl);
    connection->curl = nullptr;
    closeCode = connection->closeCode;
    reason = connection->reason;
  }
  onClose(connection, closeCode, reason);
  erase(connection);
}
} // namespace

extern "C" int wcwss_connect_socket(const std::shared_ptr<void> &callbackOwner,
                                    const std::string &client,
                                    uint32_t &socketId, const std::string &url,
                                    const StringMap &headers,
                                    const std::vector<std::string> &protocols,
                                    const OpaqueConfig &,
                                    const OpaqueConfig &) {
  if (getenv("WETYPE_DEBUG_NETWORK")) {
    auto object = callbackOwner.get();
    auto table = object ? *reinterpret_cast<void ***>(object) : nullptr;
    fprintf(stderr,
            "WSS_BRIDGE callback=%p table=%p entries=%p,%p,%p,%p client=%zu "
            "url=%zu headers=%zu protocols=%zu\n",
            object, table, table ? table[0] : nullptr,
            table ? table[1] : nullptr, table ? table[2] : nullptr,
            table ? table[3] : nullptr, client.size(), url.size(),
            headers.size(), protocols.size());
  }
  if (!callbackOwner || client.empty() ||
      (url.rfind("wss://", 0) != 0 && url.rfind("ws://", 0) != 0))
    return 100002;
  ++bridgeCalls;
  auto connection = std::make_shared<Connection>();
  connection->callback = callbackOwner;
  connection->client = client;
  connection->url = url;
  connection->headers = headers;
  connection->protocols = protocols;
  connection->id = nextId.fetch_add(1);
  socketId = connection->id;
  {
    std::lock_guard lock(connectionsMutex);
    connections[{connection->client, connection->id}] = connection;
  }
  std::thread(run, connection).detach();
  return 0;
}

extern "C" int wcwss_send_socket_message(const std::string &client,
                                         uint32_t socketId, const char *data,
                                         size_t size, bool binary) {
  std::shared_ptr<Connection> connection;
  {
    std::lock_guard lock(connectionsMutex);
    auto iterator = connections.find({client, socketId});
    if (iterator == connections.end())
      return 100003;
    connection = iterator->second;
  }
  if (!data || !size)
    return 100002;
  std::lock_guard lock(connection->curlMutex);
  if (!connection->curl || !connection->running)
    return 100002;
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(8);
  auto failed = [&](CURLcode error) {
    // A failed partial frame cannot be reused for a different message.
    connection->closeCode = error;
    connection->reason = curl_easy_strerror(error);
    connection->running = false;
    return static_cast<int>(error);
  };
  size_t offset = 0;
  while (offset < size) {
    size_t sent = 0;
    auto result = curl_ws_send(connection->curl, data + offset, size - offset,
                               &sent, 0, binary ? CURLWS_BINARY : CURLWS_TEXT);
    offset += sent;
    if (result != CURLE_OK && result != CURLE_AGAIN)
      return failed(result);
    if (offset == size) {
      ++bridgeSends;
      return 0;
    }
    if (std::chrono::steady_clock::now() >= deadline)
      return failed(CURLE_OPERATION_TIMEDOUT);
    if (result == CURLE_AGAIN || !sent)
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  return CURLE_SEND_ERROR;
}

extern "C" int wcwss_close_socket(const std::string &client, uint32_t socketId,
                                  int code, const std::string &reason) {
  std::shared_ptr<Connection> connection;
  {
    std::lock_guard lock(connectionsMutex);
    auto iterator = connections.find({client, socketId});
    if (iterator == connections.end())
      return 100003;
    connection = iterator->second;
  }
  std::lock_guard lock(connection->curlMutex);
  connection->closeCode = code;
  connection->reason = reason;
  connection->running = false;
  return 0;
}

extern "C" void wcwss_uninit(const std::string &client) {
  std::lock_guard lock(connectionsMutex);
  for (auto &[key, connection] : connections)
    if (key.first == client)
      connection->running = false;
}

extern "C" unsigned wcwss_bridge_calls() { return bridgeCalls.load(); }
extern "C" const char *wcwss_bridge_curl_version() {
  initializeCurl();
  return curl_version_info(CURLVERSION_NOW)->version;
}
extern "C" const char *wcwss_bridge_curl_provider() {
#ifdef WETYPE_BUNDLED_CURL
  return "bundled-static";
#else
  return "system";
#endif
}
extern "C" unsigned wcwss_bridge_capabilities() {
  initializeCurl();
  const auto *version = curl_version_info(CURLVERSION_NOW);
  return (supportsProtocol("ws") ? 1u : 0u) |
         (supportsProtocol("wss") ? 2u : 0u) |
         ((version->features & CURL_VERSION_IPV6) ? 4u : 0u);
}
extern "C" int wcwss_bridge_state() { return bridgeState.load(); }
extern "C" int wcwss_bridge_error() { return bridgeError.load(); }
extern "C" unsigned wcwss_bridge_sends() { return bridgeSends.load(); }
extern "C" unsigned wcwss_bridge_receives() {
  return bridgeReceives.load();
}
