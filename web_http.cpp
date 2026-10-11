// esp32-S3-ws4-caravan v1.0
/* A small HTTP server that never makes loop() wait: see web_http.h */
#include "web_http.h"
#include <lwip/sockets.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <esp_heap_caps.h>

#define READ_TIMEOUT_MS 4000    // a connection that sends no complete request in this time is dropped
#define WRITE_TIMEOUT_MS 8000   // a reply that makes no progress for this long is dropped
#define WRITE_BUDGET 2872       // two segments per slot per call: sent in step with the ACKs, so
                                // replies never pile up in lwIP's buffers (internal RAM)
#define SEND_PIECE 1436         // one TCP segment per send(): lwIP needs little memory for each
#define MEM_RESERVE 12288       // internal RAM kept free for WiFi and Bluetooth: below this,
                                // nothing new is sent or accepted until memory comes back

void logf(const char *fmt, ...);  // sdlog.cpp

/* Errors that only mean "not now": the send buffer is full, or lwIP is short of
   memory for a moment (internal RAM is scarce with WiFi, Bluetooth and the display).
   The first build closed the connection on ENOMEM, so pages arrived cut off. */
/* Internal RAM is what WiFi receives into and lwIP sends from. With the display,
   WiFi and Bluetooth running only ~35 kB is free, and a few replies in flight
   could take the rest: then WiFi drops packets and every connection stalls. */
static bool mem_low = false;  // checked once per handleClient()
static bool all_busy = false;  // a new connection is waiting for a slot

/* What happened, logged once a minute when something went wrong */
static struct {
  uint32_t replies, busy_ms, silent, stalled, errors, low_ms;
  uint32_t min_free;
} st;

static bool try_later(int e) {
  return e == EAGAIN || e == EWOULDBLOCK || e == ENOMEM || e == ENOBUFS || e == EINPROGRESS;
}

static const char *status_text(int code) {
  switch (code) {
    case 200: return "OK";
    case 204: return "No Content";
    case 303: return "See Other";
    case 304: return "Not Modified";
    case 400: return "Bad Request";
    case 401: return "Unauthorized";
    case 403: return "Forbidden";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 413: return "Payload Too Large";
    case 503: return "Service Unavailable";
    default: return "Error";
  }
}

static void set_nonblocking(int fd) {
  int fl = fcntl(fd, F_GETFL, 0);
  fcntl(fd, F_SETFL, fl | O_NONBLOCK);
}

bool MiniHttp::begin(uint16_t port) {
  if (listen_fd >= 0) return true;
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) return false;
  int one = 1;
  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
  struct sockaddr_in a;
  memset(&a, 0, sizeof(a));
  a.sin_family = AF_INET;
  a.sin_port = htons(port);
  a.sin_addr.s_addr = htonl(INADDR_ANY);
  if (bind(fd, (struct sockaddr *)&a, sizeof(a)) < 0 || listen(fd, 4) < 0) {
    close(fd);
    return false;
  }
  set_nonblocking(fd);
  listen_fd = fd;
  return true;
}

void MiniHttp::stop() {
  for (Slot &s : slot)
    if (s.state) close_slot(s);
  if (listen_fd >= 0) close(listen_fd);
  listen_fd = -1;
}

void MiniHttp::on(const char *path, Handler h) {
  if (route_count < (int)(sizeof(routes) / sizeof(routes[0]))) routes[route_count++] = { path, h };
}

void MiniHttp::close_slot(Slot &s) {
  if (s.fd >= 0) ::close(s.fd);  // lwIP still delivers what is queued
  s.fd = -1;
  s.state = 0;
  s.in = String();  // frees the memory ("" would keep it)
  s.out = String();
  s.ext = nullptr;
  s.ext_len = s.sent = 0;
  if (s.file) s.file.close();
  s.streaming = false;
  if (s.chunk) free(s.chunk);
  s.chunk = nullptr;
  s.chunk_len = s.chunk_sent = 0;
}

/* A slot for a new connection: a free one, or one that has sent nothing for a
   second (a browser's spare connection) or whose reply is already sent */
MiniHttp::Slot *MiniHttp::free_slot() {
  for (Slot &s : slot)
    if (!s.state) return &s;
  Slot *idle = nullptr;
  for (Slot &s : slot)
    if ((s.state == 3 || (s.state == 1 && !s.in.length() && millis() - s.since > 1000)) &&
        (!idle || (int32_t)(s.since - idle->since) < 0)) idle = &s;
  if (idle) {
    if (idle->state == 1) st.silent++;
    close_slot(*idle);
  }
  return idle;
}

/* New connections are only taken when there is room for them: the others wait
   in the listen queue (and the browser waits a moment) instead of getting an
   error page. */
void MiniHttp::accept_new() {
  all_busy = false;
  if (mem_low) return;  // they wait until memory comes back
  for (int tries = 0; tries < SLOTS; tries++) {
    fd_set rd;
    FD_ZERO(&rd);
    FD_SET(listen_fd, &rd);
    struct timeval tv = { 0, 0 };
    if (select(listen_fd + 1, &rd, nullptr, nullptr, &tv) <= 0) return;  // nobody waiting
    Slot *fs = free_slot();
    if (!fs) {
      all_busy = true;
      return;
    }
    struct sockaddr_in a;
    socklen_t al = sizeof(a);
    int fd = accept(listen_fd, (struct sockaddr *)&a, &al);
    if (fd < 0) return;  // nobody waiting
    set_nonblocking(fd);
    int one = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
    fs->fd = fd;
    fs->state = 1;
    fs->since = millis();
    fs->in = String();
    fs->in.reserve(512);
  }
}

void MiniHttp::read_slot(Slot &s) {
  char buf[512];
  for (;;) {
    int n = recv(s.fd, buf, sizeof(buf), MSG_DONTWAIT);
    if (n > 0) {
      s.in.concat(buf, n);
      s.since = millis();
      if (s.in.length() > MAX_REQUEST) {
        close_slot(s);  // far bigger than anything this server takes
        return;
      }
      continue;
    }
    if (n == 0 || !try_later(errno)) {
      close_slot(s);  // the client hung up, or the connection broke
      return;
    }
    break;  // nothing more for now
  }
  int he = s.in.indexOf("\r\n\r\n");
  if (he < 0) {
    if (millis() - s.since > READ_TIMEOUT_MS) {  // e.g. a browser's spare connection: normal
      st.silent++;
      close_slot(s);
    }
    return;
  }
  /* body length, if any (logins and settings are small forms) */
  size_t body_len = 0;
  String lower = s.in.substring(0, he);
  lower.toLowerCase();
  int cl = lower.indexOf("\r\ncontent-length:");
  if (cl >= 0) body_len = strtoul(lower.c_str() + cl + 17, nullptr, 10);
  if (s.in.length() < (size_t)he + 4 + body_len) {
    if (millis() - s.since > READ_TIMEOUT_MS) close_slot(s);
    return;
  }
  dispatch(s, he, body_len);
}

static String header_value(const String &lower_head, const String &head, const char *lname) {
  String key = String("\r\n") + lname + ":";
  int i = lower_head.indexOf(key);
  if (i < 0) return String();
  i += key.length();
  int e = head.indexOf("\r\n", i);
  String v = head.substring(i, e < 0 ? head.length() : e);
  v.trim();
  return v;
}

void MiniHttp::dispatch(Slot &s, size_t he, size_t body_len) {
  String head = s.in.substring(0, he);
  String lower = head;
  lower.toLowerCase();
  int sp1 = head.indexOf(' ');
  int sp2 = sp1 < 0 ? -1 : head.indexOf(' ', sp1 + 1);
  if (sp1 < 0 || sp2 < 0) {
    close_slot(s);
    return;
  }
  String m = head.substring(0, sp1);
  req_method = m == "GET" || m == "HEAD" ? MH_GET : m == "POST" ? MH_POST : MH_OTHER;
  String target = head.substring(sp1 + 1, sp2);
  int q = target.indexOf('?');
  req_path = q < 0 ? target : target.substring(0, q);
  req_query = q < 0 ? String() : target.substring(q + 1);
  req_body = s.in.substring(he + 4, he + 4 + body_len);
  req_cookie = header_value(lower, head, "cookie");
  req_inm = header_value(lower, head, "if-none-match");
  s.in = String();
  extra_headers = String();
  collecting = false;

  cur = &s;
  s.state = 2;  // a send call below fills in the reply
  Handler h = not_found;
  for (int i = 0; i < route_count; i++)
    if (req_path == routes[i].path) {
      h = routes[i].h;
      break;
    }
  if (h) h();
  else send(404, "text/plain", "Not found");
  if (collecting) sendContent("");  // a handler that forgot to end its reply
  if (!s.out.length() && !s.ext) send(500, "text/plain", "No reply");
  cur = nullptr;
  s.sent = 0;
  s.since = millis();
  st.replies++;
}

void MiniHttp::write_slot(Slot &s) {
  size_t budget = WRITE_BUDGET;
  if (mem_low) budget = 0;  // only the timeout check below
  while (budget) {
    const char *p;
    size_t left;
    if (s.sent < s.out.length()) {
      p = s.out.c_str() + s.sent;
      left = s.out.length() - s.sent;
    } else if (s.ext && s.sent < s.out.length() + s.ext_len) {
      size_t o = s.sent - s.out.length();
      p = s.ext + o;
      left = s.ext_len - o;
    } else if (s.streaming) {
      if (s.chunk_sent >= s.chunk_len) {  // next piece of the file
        s.chunk_len = s.file.read((uint8_t *)s.chunk, 1024);
        s.chunk_sent = 0;
        if (!s.chunk_len) {
          finish_slot(s);  // all queued
          return;
        }
      }
      p = s.chunk + s.chunk_sent;
      left = s.chunk_len - s.chunk_sent;
    } else {
      finish_slot(s);  // all queued
      return;
    }
    if (left > budget) left = budget;
    if (left > SEND_PIECE) left = SEND_PIECE;
    int n = ::send(s.fd, p, left, MSG_DONTWAIT);
    if (n > 0) {
      budget -= n;
      s.since = millis();
      if (s.streaming && s.sent >= s.out.length() + (s.ext ? s.ext_len : 0)) s.chunk_sent += n;
      else s.sent += n;
      continue;
    }
    if (n < 0 && try_later(errno)) break;  // the network (or lwIP's memory) is full: later
    int e = errno;
    if (n < 0 && e != ECONNRESET && e != EPIPE && e != ENOTCONN) {
      logf("Web: sending failed (errno %d) after %u bytes", e, (unsigned)s.sent);
      st.errors++;
    }
    close_slot(s);  // the client went away
    return;
  }
  if (millis() - s.since > WRITE_TIMEOUT_MS) {  // a client that stopped taking data
    st.stalled++;
    close_slot(s);
  }
}

/* Everything is handed to lwIP: say we're done (FIN) and let the client close.
   Closing at once could throw away what lwIP still has to send when the client
   had sent anything more, and the page arrived cut off. */
void MiniHttp::finish_slot(Slot &s) {
  shutdown(s.fd, SHUT_WR);
  s.state = 3;
  s.since = millis();
  s.out = String();  // lwIP has its own copy now
  s.ext = nullptr;
  if (s.file) s.file.close();
  if (s.chunk) free(s.chunk);
  s.chunk = nullptr;
  s.streaming = false;
}

void MiniHttp::drain_slot(Slot &s) {
  char buf[64];
  for (;;) {
    int n = recv(s.fd, buf, sizeof(buf), MSG_DONTWAIT);
    if (n > 0) continue;  // whatever the client still sends is ignored
    if (n == 0 || !try_later(errno)) {
      close_slot(s);  // the client closed: all delivered
      return;
    }
    break;
  }
  if (millis() - s.since > 3000) close_slot(s);  // the client keeps it open: enough
}

void MiniHttp::report() {
  uint32_t f = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
  if (!st.min_free || f < st.min_free) st.min_free = f;
  static uint32_t last_call = 0;
  if (mem_low) st.low_ms += millis() - last_call;  // how long sending was held back
  if (all_busy) st.busy_ms += millis() - last_call;  // how long a new connection waited
  last_call = millis();
  mem_low = f < MEM_RESERVE;
  static uint32_t next = 0;
  if ((int32_t)(millis() - next) < 0) return;
  next = millis() + 60000;
  if (st.busy_ms > 1000 || st.stalled || st.errors || st.low_ms)
    logf("Web, last minute: %u replies, all connections busy %u.%u s, %u stalled, %u send errors, "
         "held back %u.%u s for memory, internal RAM free %u (lowest %u)",
         (unsigned)st.replies, (unsigned)(st.busy_ms / 1000), (unsigned)(st.busy_ms % 1000 / 100),
         (unsigned)st.stalled, (unsigned)st.errors,
         (unsigned)(st.low_ms / 1000), (unsigned)(st.low_ms % 1000 / 100), (unsigned)f, (unsigned)st.min_free);
  memset(&st, 0, sizeof(st));
}

void MiniHttp::handleClient() {
  if (listen_fd < 0) return;
  report();
  accept_new();
  for (Slot &s : slot) {
    if (s.state == 1) read_slot(s);
    if (s.state == 2) write_slot(s);
    if (s.state == 3) drain_slot(s);
  }
}

/* ---------- the request ---------- */
static int hexval(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

String MiniHttp::find_param(const String &src, const char *name, bool *found) {
  size_t nl = strlen(name);
  const char *p = src.c_str();
  while (*p) {
    const char *amp = strchr(p, '&');
    size_t len = amp ? (size_t)(amp - p) : strlen(p);
    if (len >= nl && !strncmp(p, name, nl) && (len == nl || p[nl] == '=')) {
      if (found) *found = true;
      String v;
      for (size_t i = nl + 1; i < len; i++) {  // URL decoding: + and %XX
        char c = p[i];
        if (c == '+') c = ' ';
        else if (c == '%' && i + 2 < len + 1 && hexval(p[i + 1]) >= 0 && hexval(p[i + 2]) >= 0) {
          c = (char)(hexval(p[i + 1]) * 16 + hexval(p[i + 2]));
          i += 2;
        }
        v += c;
      }
      return v;
    }
    if (!amp) break;
    p = amp + 1;
  }
  if (found) *found = false;
  return String();
}

bool MiniHttp::hasArg(const char *name) const {
  bool f = false;
  find_param(req_query, name, &f);
  if (!f) find_param(req_body, name, &f);
  return f;
}

String MiniHttp::arg(const char *name) const {
  bool f = false;
  String v = find_param(req_query, name, &f);
  return f ? v : find_param(req_body, name, nullptr);
}

bool MiniHttp::hasHeader(const char *name) const {
  return header(name).length() > 0;
}

String MiniHttp::header(const char *name) const {
  if (!strcasecmp(name, "Cookie")) return req_cookie;
  if (!strcasecmp(name, "If-None-Match")) return req_inm;
  return String();
}

String MiniHttp::clientIP() const {
  if (!cur || cur->fd < 0) return String();
  struct sockaddr_in a;
  socklen_t al = sizeof(a);
  if (getpeername(cur->fd, (struct sockaddr *)&a, &al) < 0) return String();
  char b[16];
  uint32_t ip = ntohl(a.sin_addr.s_addr);
  snprintf(b, sizeof(b), "%u.%u.%u.%u", (unsigned)(ip >> 24), (unsigned)(ip >> 16) & 255, (unsigned)(ip >> 8) & 255, (unsigned)ip & 255);
  return String(b);
}

/* ---------- the reply ---------- */
void MiniHttp::sendHeader(const char *name, const String &value) {
  extra_headers += name;
  extra_headers += ": ";
  extra_headers += value;
  extra_headers += "\r\n";
}

void MiniHttp::begin_reply(int code, const char *type, size_t len) {
  if (!cur) return;
  char h[160];
  snprintf(h, sizeof(h), "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %u\r\nConnection: close\r\n",
           code, status_text(code), type, (unsigned)len);
  cur->out = String();
  cur->out.reserve(strlen(h) + extra_headers.length() + 2 + (cur->ext ? 0 : len));
  cur->out += h;
  cur->out += extra_headers;
  cur->out += "\r\n";
  extra_headers = String();
}

void MiniHttp::send(int code, const char *type, const String &body) {
  if (!cur) return;
  if (collecting) {  // setContentLength(CONTENT_LENGTH_UNKNOWN) came first: the body follows in parts
    collect_code = code;
    collect_type = type;
    req_body = body;  // the request body is no longer needed: collect the reply in it
    return;
  }
  cur->ext = nullptr;
  begin_reply(code, type, body.length());
  cur->out += body;
}

void MiniHttp::send(int code, const char *type, const char *body) {
  send(code, type, String(body ? body : ""));
}

void MiniHttp::send_P(int code, const char *type, const char *data, size_t len) {
  if (!cur) return;
  cur->ext = data;
  cur->ext_len = len;
  begin_reply(code, type, len);
}

void MiniHttp::setContentLength(size_t len) {
  collecting = (len == CONTENT_LENGTH_UNKNOWN);
}

void MiniHttp::sendContent(const String &part) {
  if (!cur || !collecting) return;
  if (part.length()) {
    req_body += part;
    return;
  }
  collecting = false;  // an empty part ends the reply: now its length is known
  String body = req_body;
  req_body = String();
  send(collect_code, collect_type.c_str(), body);
}

void MiniHttp::sendContent(const char *part) {
  sendContent(String(part ? part : ""));
}

void MiniHttp::streamFile(fs::File &f, const char *type) {
  if (!cur) return;
  cur->ext = nullptr;
  begin_reply(200, type, f.size());
  cur->chunk = (char *)heap_caps_malloc(1024, MALLOC_CAP_SPIRAM);
  if (!cur->chunk) cur->chunk = (char *)malloc(1024);
  if (!cur->chunk) return;  // only the headers go out; the client sees a short file
  cur->file = f;
  cur->streaming = true;
  cur->chunk_len = cur->chunk_sent = 0;
}
