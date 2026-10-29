#include <WinSock2.h>
#include <Windows.h>
#include "transport.h"
#include "socket_bridge.h"
#include "directory.h"
#include "session_publisher.h"
#include <fstream>
#include <iostream>
#include <thread>

using namespace redbaron::net;
extern "C" __declspec(dllimport) void RedBaronInitCryptoThreading();
int main(int argc,char** argv) {
  try {
    if(argc!=3 || (std::string(argv[1])!="--config" && std::string(argv[1])!="--list")) {
      std::cerr<<"Usage: redbaron_net --config session.json | --list directory.json\n"; return 2;
    }
    std::ifstream file(argv[2]);if(!file)throw std::runtime_error("Session configuration was not found");
    auto options=Json::parse(file);
    if(std::string(argv[1])=="--list") { std::cout<<ReadDirectory(options).dump()<<std::endl; return 0; }
    int ns=options.value("namespace",120);
    if(ns<1 || ns>254)throw std::runtime_error("Invalid local network namespace");
    RedBaronInitCryptoThreading();
    rtc::InitLogger(rtc::LogLevel::Warning,[](rtc::LogLevel level,std::string message){std::cerr<<message<<'\n';});
    Transport transport(options);
    SocketBridge bridge(transport,uint8_t(ns),options.value("diagnostics",false));
    std::unique_ptr<SessionPublisher> publisher;
    if(options.value("public",false) && options.value("room","").empty())
      publisher=std::make_unique<SessionPublisher>(transport,uint8_t(ns));
    transport.status=[&](const Json& message){
      Json output=message;
      if(message.at("op")=="welcome") {
        output["bind"]=bridge.Address(transport.id());
        output["host_address"]=bridge.Address(0);
        output["namespace"]=ns;
      }
      std::cout<<output.dump()<<std::endl;
    };
    transport.membership=[&](uint16_t id,bool present){bridge.Member(id,present);};
    transport.packet=[&](uint16_t id,const Bytes& bytes,bool reliable){bridge.Packet(id,bytes,reliable);};
    const HANDLE input=GetStdHandle(STD_INPUT_HANDLE);
    const bool piped=GetFileType(input)==FILE_TYPE_PIPE;
    HANDLE parent=nullptr;
    if(auto pid=options.value("parent_pid",0u)) {
      parent=OpenProcess(SYNCHRONIZE,FALSE,pid);
      if(!parent)throw std::runtime_error("Launcher is no longer running");
    }
    std::string commands;
    // Sleep(1) may round to 15.6 ms on Windows, adding ~60 ms to a bridged RTT.
    // A high-resolution waitable timer avoids changing the system timer period.
    HANDLE timer=CreateWaitableTimerExW(nullptr,nullptr,CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,TIMER_ALL_ACCESS);
    if(!timer)throw std::runtime_error("A high-resolution Windows timer is required for online play");
    while(!transport.stopped()) {
      transport.Poll();
      if(transport.admitted())bridge.Poll();
      if(publisher)publisher->Poll();
      if(parent && WaitForSingleObject(parent,0)==WAIT_OBJECT_0)break;
      if(piped) {
        DWORD available=0;
        if(!PeekNamedPipe(input,nullptr,0,nullptr,&available,nullptr))break;
        if(available) {
          char text[2048];DWORD count=0;
          if(!ReadFile(input,text,std::min<DWORD>(available,sizeof(text)),&count,nullptr))break;
          commands.append(text,count);
          if(commands.size()>8192)throw std::runtime_error("Launcher command is too large");
          size_t end;
          while((end=commands.find('\n'))!=std::string::npos) {
            const auto command=Json::parse(commands.substr(0,end));commands.erase(0,end+1);
            if(command.value("op","")=="stop")transport.Stop();
          }
        }
      }
      LARGE_INTEGER due{};due.QuadPart=-10000;
      if(!SetWaitableTimer(timer,&due,0,nullptr,nullptr,FALSE))break;
      WaitForSingleObject(timer,INFINITE);
    }
    if(parent)CloseHandle(parent);
    CloseHandle(timer);
    return 0;
  } catch(const std::exception& error) {
    std::cout<<Json{{"op","error"},{"message",error.what()}}.dump()<<std::endl;
    return 1;
  }
}
