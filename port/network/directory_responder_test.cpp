#include <WinSock2.h>
#include <WS2tcpip.h>
#include "directory_responder.h"
#include <algorithm>
#include <array>
#include <iostream>
#include <stdexcept>
#include <string>

void Check(bool ok){if(!ok)throw std::runtime_error("Local directory responder test failed");}
int main(){
  try {
    WSADATA data{};Check(WSAStartup(MAKEWORD(2,2),&data)==0);
    {
      redbaron::net::DirectoryResponder responder(196);
      std::array<unsigned char,104> record{};
      const char prefix[]="RBLAN01";std::copy_n(prefix,7,record.begin());record[7]=2;
      record[16]=0x58;record[17]=0x41;record[18]=0x09;record[19]=0xb5;record[83]=8;
      constexpr char digits[]="0123456789abcdef";std::string hex;
      for(auto b:record){hex.push_back(digits[b>>4]);hex.push_back(digits[b&15]);}
      auto other=hex;other[41]='1'; // A distinct native session ID.
      Check(!responder.Set(1,hex));Check(!responder.Set(256,"invalid"));
      Check(responder.Set(256,hex) && responder.Set(257,other));
      SOCKET client=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);Check(client!=INVALID_SOCKET);
      sockaddr_in own{};own.sin_family=AF_INET;own.sin_addr.s_addr=htonl(0x7fc40001u);
      Check(bind(client,reinterpret_cast<sockaddr*>(&own),sizeof(own))==0);
      sockaddr_in dest{};dest.sin_family=AF_INET;dest.sin_addr.s_addr=htonl(0x7fc40000u);dest.sin_port=htons(43821);
      std::array<unsigned char,20> query{'R','B','L','A','N','0','1',1,1,2,3,4,5,6,7,8,0x58,0x41,0x09,0xb5};
      Check(sendto(client,reinterpret_cast<const char*>(query.data()),int(query.size()),0,reinterpret_cast<sockaddr*>(&dest),sizeof(dest))==20);
      responder.Poll();
      std::array<bool,2> seen{};
      for(int i=0;i<2;++i){
        fd_set reads;FD_ZERO(&reads);FD_SET(client,&reads);timeval timeout{1,0};Check(select(0,&reads,nullptr,nullptr,&timeout)>0);
        std::array<unsigned char,8192> packet{};sockaddr_in from{};int from_size=sizeof(from);
        const auto size=recvfrom(client,reinterpret_cast<char*>(packet.data()),int(packet.size()),0,reinterpret_cast<sockaddr*>(&from),&from_size);
        const auto id=ntohl(from.sin_addr.s_addr)&0xffff;Check(size==104 && id>=256 && id<=257 && !seen[id-256]);seen[id-256]=true;
        Check(std::equal(query.begin()+8,query.begin()+16,packet.begin()+8));
      }
      responder.Remove(256);responder.Remove(257);closesocket(client);
    }
    WSACleanup();std::cout<<"PASS: multiple validated sessions returned over local discovery sockets\n";return 0;
  }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
