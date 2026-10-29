#include "directory_mapping.h"
#include <array>
#include <algorithm>
#include <iostream>
#include <stdexcept>

void Check(bool condition) {if(!condition)throw std::runtime_error("Directory mapping test failed");}
std::string Record(unsigned id) {
  std::array<unsigned char,104> bytes{};const char prefix[]="RBLAN01";
  std::copy_n(prefix,7,bytes.begin());bytes[7]=2;bytes[16]=0x58;bytes[17]=0x41;bytes[18]=0x09;bytes[19]=0xb5;
  bytes[20]=static_cast<unsigned char>(id);bytes[83]=8;
  constexpr char hex[]="0123456789abcdef";std::string result;
  for(auto value:bytes){result.push_back(hex[value>>4]);result.push_back(hex[value&15]);}return result;
}
int main() {
  try {
    redbaron::net::DirectoryResponder responder(195);redbaron::eos::DirectoryMapping mapping(responder);
    redbaron::eos::LobbyEntry a{"a",Record(1)},b{"b",Record(2)},c{"c",Record(3)};
    Check(mapping.Update({a,b}));Check(mapping.LobbyAt(256) && *mapping.LobbyAt(256)=="a");
    Check(mapping.LobbyAt(257) && *mapping.LobbyAt(257)=="b");
    Check(mapping.Update({b,c}));Check(mapping.LobbyAt(257) && *mapping.LobbyAt(257)=="b");
    Check(mapping.LobbyAt(256) && *mapping.LobbyAt(256)=="c");
    auto duplicate=b;duplicate.id="different";
    Check(!mapping.Update({b,duplicate}));Check(*mapping.LobbyAt(257)=="b");
    Check(mapping.Update({}));Check(!mapping.LobbyAt(256) && !mapping.LobbyAt(257));
    std::cout<<"PASS: stable room mapping and duplicate session rejection\n";return 0;
  } catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
