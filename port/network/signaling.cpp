#include <Windows.h>
#include <winhttp.h>
#include "signaling.h"
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <stdexcept>
#include <thread>

namespace redbaron::net {
namespace {
void Check(bool value,const char* operation) {
  if(!value)throw std::runtime_error(std::string(operation)+" failed (Windows error "+std::to_string(GetLastError())+")");
}
std::wstring Wide(const std::string& text) {
  int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),int(text.size()),nullptr,0);
  if(!n)throw std::runtime_error("Invalid server URL");
  std::wstring out(n,L'\0');MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),int(text.size()),out.data(),n);return out;
}
struct Handle {
  HINTERNET value=nullptr;
  ~Handle(){if(value)WinHttpCloseHandle(value);}
};
}
struct Signaling::Impl {
  std::atomic<bool> stop{false};
  std::mutex mutex;
  std::condition_variable wake;
  std::deque<std::string> outgoing;
  HINTERNET active=nullptr;
  std::thread reader,writer;
  std::function<void()> opened;
  std::function<void(std::string)> message,closed;
  Impl(std::string url,std::function<void()> on_open,std::function<void(std::string)> on_message,
       std::function<void(std::string)> on_close):opened(std::move(on_open)),message(std::move(on_message)),closed(std::move(on_close)) {
    reader=std::thread([this,url]{Run(url);});
  }
  void Close() {
    stop=true;wake.notify_all();
    {std::lock_guard lock(mutex);if(active){WinHttpCloseHandle(active);active=nullptr;}}
  }
  ~Impl(){Close();if(reader.joinable())reader.join();if(writer.joinable())writer.join();}
  void Run(std::string url) {
    try {
      if(url.starts_with("wss://"))url.replace(0,6,"https://");
      else if(url.starts_with("ws://"))url.replace(0,5,"http://");
      auto wide=Wide(url);URL_COMPONENTS parts{};parts.dwStructSize=sizeof(parts);
      parts.dwHostNameLength=parts.dwUrlPathLength=parts.dwExtraInfoLength=DWORD(-1);
      Check(WinHttpCrackUrl(wide.data(),DWORD(wide.size()),0,&parts),"Server URL");
      std::wstring hostname(parts.lpszHostName,parts.dwHostNameLength);
      std::wstring path(parts.lpszUrlPath,parts.dwUrlPathLength);
      if(parts.dwExtraInfoLength)path.append(parts.lpszExtraInfo,parts.dwExtraInfoLength);
      if(path.empty())path=L"/";
      Handle session{WinHttpOpen(L"RedBaron_Recomp/1",WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                 WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0)};
      Check(session.value,"WinHTTP session");
      Check(WinHttpSetTimeouts(session.value,10000,10000,10000,25000),"Network timeouts");
      Handle connection{WinHttpConnect(session.value,hostname.c_str(),parts.nPort,0)};
      Check(connection.value,"Server connection");
      Handle request{WinHttpOpenRequest(connection.value,L"GET",path.c_str(),nullptr,
                                        WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,
                                        parts.nScheme==INTERNET_SCHEME_HTTPS?WINHTTP_FLAG_SECURE:0)};
      Check(request.value,"WebSocket request");
      DWORD redirect=WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
      Check(WinHttpSetOption(request.value,WINHTTP_OPTION_REDIRECT_POLICY,&redirect,sizeof(redirect)),"Redirect policy");
      Check(WinHttpSetOption(request.value,WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET,nullptr,0),"WebSocket upgrade");
      Check(WinHttpSendRequest(request.value,WINHTTP_NO_ADDITIONAL_HEADERS,0,WINHTTP_NO_REQUEST_DATA,0,0,0),"TLS/server handshake");
      Check(WinHttpReceiveResponse(request.value,nullptr),"Server response");
      DWORD status=0,size=sizeof(status);
      Check(WinHttpQueryHeaders(request.value,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,
                               WINHTTP_HEADER_NAME_BY_INDEX,&status,&size,WINHTTP_NO_HEADER_INDEX),"HTTP status");
      if(status!=101)throw std::runtime_error("Room server refused WebSocket upgrade (HTTP "+std::to_string(status)+")");
      HINTERNET socket=WinHttpWebSocketCompleteUpgrade(request.value,0);
      Check(socket,"WebSocket completion");
      {std::lock_guard lock(mutex);if(stop){WinHttpCloseHandle(socket);return;}active=socket;}
      writer=std::thread([this,socket] {
        while(!stop) {
          std::string data;
          {std::unique_lock lock(mutex);wake.wait(lock,[this]{return stop || !outgoing.empty();});
           if(stop)break;data=std::move(outgoing.front());outgoing.pop_front();}
          DWORD error=WinHttpWebSocketSend(socket,WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE,data.data(),DWORD(data.size()));
          if(error){closed("Room send failed (Windows error "+std::to_string(error)+")");Close();break;}
        }
      });
      opened();
      std::string incoming;
      while(!stop) {
        char buffer[8192];DWORD count=0;WINHTTP_WEB_SOCKET_BUFFER_TYPE type;
        DWORD error=WinHttpWebSocketReceive(socket,buffer,sizeof(buffer),&count,&type);
        if(error){if(!stop)throw std::runtime_error("Room receive failed (Windows error "+std::to_string(error)+")");break;}
        if(type==WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE)break;
        if(type!=WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE && type!=WINHTTP_WEB_SOCKET_UTF8_FRAGMENT_BUFFER_TYPE)
          throw std::runtime_error("Invalid room server message");
        if(incoming.size()+count>49152)throw std::runtime_error("Room server message is too large");
        incoming.append(buffer,count);
        if(type==WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE){message(std::move(incoming));incoming.clear();}
      }
      if(!stop)closed("Room connection closed");
    }catch(const std::exception& error){if(!stop)closed(error.what());}
    Close();
  }
  void Send(std::string text) {
    {std::lock_guard lock(mutex);
     if(stop)throw std::runtime_error("Room connection is closed");
     if(outgoing.size()>=128 || text.size()>49152)throw std::runtime_error("Room signaling queue is full");
     outgoing.push_back(std::move(text));}
    wake.notify_one();
  }
};
Signaling::Signaling(std::string url,std::function<void()> opened,std::function<void(std::string)> message,
                     std::function<void(std::string)> closed)
    :impl_(std::make_unique<Impl>(url,std::move(opened),std::move(message),std::move(closed))){}
Signaling::~Signaling()=default;
void Signaling::Send(std::string text){impl_->Send(std::move(text));}
}
