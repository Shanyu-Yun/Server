#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

#include <functional>
#include <string>
#include <thread>

#include "base/logger.hpp"
#include "http/httprequest.hpp"
#include "http/httpresponse.hpp"
#include "http/httpserver.hpp"
#include "net/eventloop.hpp"
#include "net/inetaddress.hpp"

using namespace net;

// ── HTML 页面（内嵌，零外部文件依赖）──────────────────────────────────────────
static const std::string kHtmlPage = R"html(<!DOCTYPE html>
<html lang="zh">
<head>
<meta charset="utf-8">
<title>tinynet AI Chat</title>
<style>
  * { box-sizing: border-box; margin: 0; padding: 0; }
  body { font-family: system-ui, sans-serif; background: #f5f5f5; display: flex; justify-content: center; padding: 40px 16px; }
  .container { width: 100%; max-width: 720px; display: flex; flex-direction: column; gap: 12px; }
  h2 { color: #333; font-size: 1.2rem; }
  #chat { background: #fff; border: 1px solid #ddd; border-radius: 8px; height: 480px; overflow-y: auto; padding: 16px; display: flex; flex-direction: column; gap: 10px; }
  .msg-user { align-self: flex-end; background: #0066cc; color: #fff; padding: 8px 14px; border-radius: 16px 16px 4px 16px; max-width: 80%; }
  .msg-ai { align-self: flex-start; background: #eee; color: #333; padding: 8px 14px; border-radius: 16px 16px 16px 4px; max-width: 80%; white-space: pre-wrap; line-height: 1.6; }
  .msg-ai.thinking { color: #888; font-size: 0.85em; font-style: italic; }
  #input-row { display: flex; gap: 8px; }
  #input { flex: 1; padding: 10px 14px; border: 1px solid #ddd; border-radius: 8px; font-size: 1rem; outline: none; }
  #input:focus { border-color: #0066cc; }
  button { padding: 10px 20px; background: #0066cc; color: #fff; border: none; border-radius: 8px; cursor: pointer; font-size: 1rem; }
  button:disabled { background: #aaa; cursor: not-allowed; }
</style>
</head>
<body>
<div class="container">
  <h2>tinynet AI Chat &nbsp;·&nbsp; Qwen SSE</h2>
  <div id="chat"></div>
  <div id="input-row">
    <input id="input" placeholder="输入消息，回车发送…" autofocus />
    <button id="btn" onclick="send()">发送</button>
  </div>
</div>
<script>
const chat = document.getElementById('chat');
const input = document.getElementById('input');
const btn = document.getElementById('btn');

input.addEventListener('keydown', e => { if (e.key === 'Enter' && !e.shiftKey) send(); });

async function send() {
  const text = input.value.trim();
  if (!text) return;
  input.value = '';
  btn.disabled = true;

  // 用户气泡
  const userDiv = document.createElement('div');
  userDiv.className = 'msg-user';
  userDiv.textContent = text;
  chat.appendChild(userDiv);

  // AI 气泡（流式填入）
  const aiDiv = document.createElement('div');
  aiDiv.className = 'msg-ai';
  chat.appendChild(aiDiv);
  chat.scrollTop = chat.scrollHeight;

  try {
    const resp = await fetch('/chat', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ message: text })
    });

    const reader = resp.body.getReader();
    const decoder = new TextDecoder();
    let buf = '';

    while (true) {
      const { done, value } = await reader.read();
      if (done) break;
      buf += decoder.decode(value, { stream: true });

      // 按 \n\n 切分 SSE 帧
      let idx;
      while ((idx = buf.indexOf('\n\n')) !== -1) {
        const frame = buf.slice(0, idx);
        buf = buf.slice(idx + 2);
        if (!frame.startsWith('data: ')) continue;
        const data = frame.slice(6);
        if (data === '[DONE]') { reader.cancel(); return; }
        if (data === '[ERROR]') { aiDiv.textContent += '\n[服务端错误]'; reader.cancel(); return; }
        aiDiv.textContent += data;
        chat.scrollTop = chat.scrollHeight;
      }
    }
  } catch (e) {
    aiDiv.textContent = '[请求失败: ' + e.message + ']';
  } finally {
    btn.disabled = false;
    chat.scrollTop = chat.scrollHeight;
  }
}
</script>
</body>
</html>)html";

// ── JSON 工具（不依赖第三方库）──────────────────────────────────────────────

// 从 JSON 字符串中提取指定 key 的字符串值，处理基本转义和 \uXXXX
static std::string extractJsonString(const std::string& json, const std::string& key) {
  std::string search = "\"" + key + "\":\"";
  auto pos = json.find(search);
  if (pos == std::string::npos) return "";
  pos += search.size();

  std::string result;
  while (pos < json.size()) {
    if (json[pos] == '\\' && pos + 1 < json.size()) {
      char next = json[pos + 1];
      if      (next == '"')  { result += '"';  pos += 2; }
      else if (next == '\\') { result += '\\'; pos += 2; }
      else if (next == 'n')  { result += '\n'; pos += 2; }
      else if (next == 'r')  { result += '\r'; pos += 2; }
      else if (next == 't')  { result += '\t'; pos += 2; }
      else if (next == 'u' && pos + 5 < json.size()) {
        // \uXXXX → UTF-8
        uint32_t cp = std::stoul(json.substr(pos + 2, 4), nullptr, 16);
        if      (cp < 0x80)  { result += (char)cp; }
        else if (cp < 0x800) { result += (char)(0xC0|(cp>>6)); result += (char)(0x80|(cp&0x3F)); }
        else                 { result += (char)(0xE0|(cp>>12)); result += (char)(0x80|((cp>>6)&0x3F)); result += (char)(0x80|(cp&0x3F)); }
        pos += 6;
      } else { result += next; pos += 2; }
    } else if (json[pos] == '"') {
      break;
    } else {
      result += json[pos++];
    }
  }
  return result;
}

// 对用户输入做 JSON 转义，防止注入
static std::string escapeJson(const std::string& s) {
  std::string r;
  for (unsigned char c : s) {
    switch (c) {
      case '"':  r += "\\\""; break;
      case '\\': r += "\\\\"; break;
      case '\n': r += "\\n";  break;
      case '\r': r += "\\r";  break;
      case '\t': r += "\\t";  break;
      default:   r += (char)c;
    }
  }
  return r;
}

// 从 llama-server SSE 帧的 JSON 中提取 token（跳过 reasoning_content 思维链）
static std::string extractSseToken(const std::string& data) {
  if (data.find("reasoning_content") != std::string::npos) return "";
  return extractJsonString(data, "content");
}

// ── llama-server 出站 HTTP 客户端 ────────────────────────────────────────────
// 在独立线程中调用；onToken 返回 false 表示连接已断，停止推理
static void callLlamaStream(const std::string& message,
                             std::function<bool(const std::string&)> onToken) {
  int fd = ::socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) { LOGERROR("callLlamaStream: socket()"); return; }

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port   = htons(8081);
  ::inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

  if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
    LOGERROR("callLlamaStream: connect to 127.0.0.1:8081 failed — is llama-server running?");
    ::close(fd);
    onToken("[ERROR]");
    return;
  }

  std::string body =
    R"({"model":"qwen","stream":true,"max_tokens":1024,)"
    R"("chat_template_kwargs":{"enable_thinking":false},)"
    R"("messages":[{"role":"user","content":")" + escapeJson(message) + R"("}]})";

  std::string req =
    "POST /v1/chat/completions HTTP/1.1\r\n"
    "Host: 127.0.0.1:8081\r\n"
    "Content-Type: application/json\r\n"
    "Content-Length: " + std::to_string(body.size()) + "\r\n"
    "Connection: close\r\n"
    "\r\n" + body;

  ::send(fd, req.data(), req.size(), 0);

  // 读响应：跳过 HTTP 头，然后逐行解析 SSE 帧
  std::string buf;
  bool headersDone = false;
  char tmp[4096];

  while (true) {
    ssize_t n = ::recv(fd, tmp, sizeof(tmp), 0);
    if (n <= 0) break;
    buf.append(tmp, static_cast<size_t>(n));

    if (!headersDone) {
      auto pos = buf.find("\r\n\r\n");
      if (pos == std::string::npos) continue;
      buf = buf.substr(pos + 4);
      headersDone = true;
    }

    bool stop = false;
    while (!stop) {
      auto lineEnd = buf.find('\n');
      if (lineEnd == std::string::npos) break;

      std::string line = buf.substr(0, lineEnd);
      buf = buf.substr(lineEnd + 1);
      if (!line.empty() && line.back() == '\r') line.pop_back();
      if (line.empty()) continue;

      // 跳过 chunked 编码的 size 行（纯十六进制字符）
      bool isHex = true;
      for (char c : line) {
        if (!isxdigit(static_cast<unsigned char>(c))) { isHex = false; break; }
      }
      if (isHex) continue;

      if (line == "data: [DONE]") { stop = true; break; }

      if (line.size() > 6 && line.substr(0, 6) == "data: ") {
        std::string token = extractSseToken(line.substr(6));
        if (!token.empty() && !onToken(token)) { stop = true; break; }
      }
    }
    if (stop) break;
  }

  ::close(fd);
}

// ── 路由 ─────────────────────────────────────────────────────────────────────

static void onRequest(const http::HttpRequest& req, http::HttpResponse* resp) {
  if (req.path() == "/") {
    resp->setStatusCode(http::k200Ok);
    resp->setContentType("text/html; charset=utf-8");
    resp->setBody(kHtmlPage);

  } else if (req.path() == "/chat" && req.method() == http::Method::kPost) {
    resp->setStatusCode(http::k200Ok);
    resp->setContentType("text/event-stream");
    resp->addHeader("Cache-Control", "no-cache");
    resp->setStreaming(true);
    resp->setCloseConnection(false);

  } else {
    resp->setStatusCode(http::k404NotFound);
    resp->setContentType("text/plain");
    resp->setBody("404 Not Found\r\n");
  }
}

static void onStream(const http::HttpRequest& req,
                     http::HttpResponse*,
                     const TcpConnectionPtr& conn) {
  if (req.path() != "/chat") return;

  std::string message = extractJsonString(req.body(), "message");
  if (message.empty()) {
    conn->send(http::HttpServer::makeSseFrame("[DONE]"));
    return;
  }

  LOGINFO("llm stream: message={}", message);
  WeakTcpConnectionPtr weakConn = conn;

  std::thread([weakConn, message]() {
    callLlamaStream(message, [&weakConn](const std::string& token) -> bool {
      auto c = weakConn.lock();
      if (!c) return false;  // 浏览器已断开，停止推理
      c->send(http::HttpServer::makeSseFrame(token));
      return true;
    });
    if (auto c = weakConn.lock()) {
      c->send(http::HttpServer::makeSseFrame("[DONE]"));
    }
  }).detach();
}

// ── main ─────────────────────────────────────────────────────────────────────

int main() {
  EventLoop loop;
  InetAddress listenAddr(8080, "127.0.0.1");

  http::HttpServer server(&loop, listenAddr, "llm-chat");
  server.setHttpCallback(onRequest);
  server.setStreamCallback(onStream);
  server.setThreadNum(2);
  server.start();

  LOGINFO("llm-chat listening on http://127.0.0.1:8080");
  LOGINFO("请先启动 llama-server:");
  LOGINFO("  /home/summer/GitRepo/llama.cpp/build/bin/llama-server \\");
  LOGINFO("    -m /home/summer/models/Qwen3.5-4B-Uncensored-HauhauCS-Aggressive-Q4_K_M.gguf \\");
  LOGINFO("    --host 127.0.0.1 --port 8081 -ngl 99");

  loop.loop();
  return 0;
}
