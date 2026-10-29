#include "directory_mapping.h"
#include "../network/session_record.h"
#include <algorithm>
#include <set>

namespace redbaron::eos {
bool DirectoryMapping::Update(const std::vector<LobbyEntry>& entries) {
  if(entries.size()>32)return false;
  std::set<std::string> ids,session_ids;
  for(const auto& entry:entries) {
    if(entry.id.empty() || !redbaron::net::ValidSessionRecord(entry.record) ||
       !ids.insert(entry.id).second || !session_ids.insert(entry.record.substr(40,16)).second)return false;
  }
  std::map<std::string,Room> next;
  std::set<uint16_t> used;
  for(const auto& entry:entries) {
    auto it=rooms_.find(entry.id);
    if(it!=rooms_.end()) {next.emplace(entry.id,Room{it->second.address,entry.record});used.insert(it->second.address);}
  }
  for(const auto& entry:entries) if(!next.contains(entry.id)) {
    uint16_t address=256;while(address<288 && used.contains(address))++address;
    if(address>=288)return false;
    next.emplace(entry.id,Room{address,entry.record});used.insert(address);
  }
  // Retire changed addresses before opening new sockets. On failure restore the
  // previous snapshot; if restoration itself fails the caller must stop use.
  for(const auto& [id,room]:rooms_) if(!next.contains(id))responder_.Remove(room.address);
  try {
    for(const auto& [id,room]:next) if(!responder_.Set(room.address,room.record))throw 0;
  } catch(...) {
    for(const auto& [id,room]:next)responder_.Remove(room.address);
    try {for(const auto& [id,room]:rooms_)if(!responder_.Set(room.address,room.record))return false;}
    catch(...) {for(const auto& [id,room]:rooms_)responder_.Remove(room.address);rooms_.clear();return false;}
    return false;
  }
  rooms_=std::move(next);return true;
}
const std::string* DirectoryMapping::LobbyAt(uint16_t address) const {
  auto found=std::find_if(rooms_.begin(),rooms_.end(),[address](const auto& item){return item.second.address==address;});
  return found==rooms_.end() ? nullptr : &found->first;
}
}
