// esp32-S3-ws4-caravan v1.0
#pragma once
/* A small HTTP/1.0 server that never makes loop() wait.
 *
 * Why not Arduino's WebServer: it serves one connection at a time and waits on
 * it. A browser that opens a spare connection and sends nothing (Chrome does
 * this to be quick next time) holds the server for 5 s, and a phone that goes
 * to sleep halfway through a reply can hold it for 10 s per piece written. As
 * the screen is drawn from the same loop(), the display froze and the page
 * stopped loading.
 *
 * Here every connection has a slot. Requests are read and replies written a
 * little at a time, only as far as the network accepts without waiting, so a
 * slow or silent client delays nobody. Replies go out two segments at a time and
 * wait while internal RAM is short, so they never starve WiFi of memory. Each reply is built in full first and
 * sent with its length, then the connection is closed. Slots that stall are
 * dropped after a few seconds; while all are busy, new connections wait in the
 * listen queue.
 *
 * The calls mirror the parts of WebServer this project used. */
#include <Arduino.h>
#include <FS.h>

#ifndef CONTENT_LENGTH_UNKNOWN
#define CONTENT_LENGTH_UNKNOWN ((size_t)-1)
#endif

enum MiniHttpMethod { MH_GET,
                      MH_POST,
                      MH_OTHER };

class MiniHttp {
 public:
  typedef void (*Handler)();

  bool begin(uint16_t port = 80);
  void stop();
  bool running() const { return listen_fd >= 0; }
  void handleClient();  // call often; never blocks

  void on(const char *path, Handler h);
  void onNotFound(Handler h) { not_found = h; }

  /* the request being handled */
  MiniHttpMethod method() const { return req_method; }
  const String &uri() const { return req_path; }
  bool hasArg(const char *name) const;
  String arg(const char *name) const;
  bool hasHeader(const char *name) const;
  String header(const char *name) const;  // "Cookie" and "If-None-Match" are kept
  String clientIP() const;

  /* the reply: headers first, then one of the send calls */
  void sendHeader(const char *name, const String &value);
  void send(int code, const char *type, const String &body);
  void send(int code, const char *type, const char *body = "");
  void send_P(int code, const char *type, const char *data, size_t len);  // data must stay valid (a constant)
  void setContentLength(size_t len);                                      // CONTENT_LENGTH_UNKNOWN: then sendContent()
  void sendContent(const String &part);                                   // collected; sent in full when it ends
  void sendContent(const char *part);
  void streamFile(fs::File &f, const char *type);  // sent in pieces as the client takes them

 private:
  static const int SLOTS = 6;  // lwIP has 16 sockets in all; WiFi, mDNS and HTTP use some
  static const size_t MAX_REQUEST = 4096;
  struct Slot {
    int fd = -1;
    uint8_t state = 0;  // 0 free, 1 reading, 2 writing, 3 sent: waiting for the client to close
    uint32_t since = 0;  // last progress
    String in;           // request as received
    String out;          // reply (headers + body, or headers only when streaming)
    const char *ext = nullptr;  // body sent from a constant instead of out
    size_t ext_len = 0;
    size_t sent = 0;     // of out, then of ext
    fs::File file;       // streamed after out
    bool streaming = false;
    char *chunk = nullptr;  // 1 kB in PSRAM while a file is streamed
    size_t chunk_len = 0, chunk_sent = 0;
  };
  Slot slot[SLOTS];
  int listen_fd = -1;

  struct Route {
    const char *path;
    Handler h;
  };
  Route routes[16];
  int route_count = 0;
  Handler not_found = nullptr;

  /* request being handled */
  Slot *cur = nullptr;
  MiniHttpMethod req_method = MH_GET;
  String req_path, req_query, req_body, req_cookie, req_inm;
  String extra_headers;
  bool collecting = false;
  int collect_code = 200;
  String collect_type;

  void accept_new();
  Slot *free_slot();
  void read_slot(Slot &s);
  void write_slot(Slot &s);
  void close_slot(Slot &s);
  void finish_slot(Slot &s);
  void drain_slot(Slot &s);
  void report();  // a line in the log once a minute when something went wrong
  void dispatch(Slot &s, size_t header_end, size_t body_len);
  void begin_reply(int code, const char *type, size_t len);
  static String find_param(const String &src, const char *name, bool *found);
};
