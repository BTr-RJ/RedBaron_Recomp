#include "roster.h"
#include <algorithm>
#include <set>
#include <stdexcept>

namespace redbaron::eos {
namespace {
bool ValidId(std::string_view id) {
  return !id.empty() && id!="-" && id.size()<=32 && std::all_of(id.begin(),id.end(),[](unsigned char c){
    return c>=33 && c<=126 && c!=',';
  });
}
bool ValidMembers(std::string_view owner,const std::vector<std::string>& members) {
  if(!ValidId(owner) || members.empty() || members.size()>8)return false;
  std::set<std::string> unique;
  for(const auto& member:members)if(!ValidId(member) || !unique.insert(member).second)return false;
  return unique.contains(std::string(owner));
}
}
std::optional<Roster> AssignRoster(const Roster& previous,std::string_view owner,const std::vector<std::string>& members) {
  if(!ValidMembers(owner,members))return std::nullopt;
  Roster out{};out[0]=owner;std::set<std::string> assigned{std::string(owner)};
  for(size_t i=1;i<out.size();++i) {
    if(previous[i].empty() || previous[i]==owner || std::find(members.begin(),members.end(),previous[i])==members.end())continue;
    if(!assigned.insert(previous[i]).second)return std::nullopt;
    out[i]=previous[i];
  }
  for(const auto& member:members)if(!assigned.contains(member)) {
    auto free=std::find(out.begin()+1,out.end(),std::string{});if(free==out.end())return std::nullopt;
    *free=member;assigned.insert(member);
  }
  return out;
}
std::array<std::string,2> EncodeRoster(const Roster& roster) {
  std::array<std::string,2> encoded;
  for(size_t i=0;i<roster.size();++i) {
    if(!roster[i].empty() && !ValidId(roster[i]))throw std::invalid_argument("Invalid roster identity");
    auto& value=encoded[i/4];if(i%4)value.push_back(',');value+=roster[i].empty() ? "-" : roster[i];
  }
  return encoded;
}
std::optional<Roster> DecodeRoster(const std::array<std::string,2>& encoded,std::string_view owner,
                                   const std::vector<std::string>& members) {
  if(!ValidMembers(owner,members))return std::nullopt;
  Roster out{};
  for(size_t group=0;group<2;++group) {
    if(encoded[group].empty() || encoded[group].size()>131)return std::nullopt;
    size_t pos=0;
    for(size_t i=0;i<4;++i) {
      const size_t end=encoded[group].find(',',pos);
      if((i<3 && end==std::string::npos) || (i==3 && end!=std::string::npos))return std::nullopt;
      const auto part=encoded[group].substr(pos,end==std::string::npos ? end : end-pos);
      if(part!="-") {if(!ValidId(part))return std::nullopt;out[group*4+i]=part;}
      pos=end==std::string::npos ? encoded[group].size() : end+1;
    }
  }
  if(out[0]!=owner)return std::nullopt;
  std::set<std::string> observed;
  for(const auto& id:out)if(!id.empty() &&
      (std::find(members.begin(),members.end(),id)==members.end() || !observed.insert(id).second))return std::nullopt;
  if(observed.size()!=members.size())return std::nullopt;
  return out;
}
}
