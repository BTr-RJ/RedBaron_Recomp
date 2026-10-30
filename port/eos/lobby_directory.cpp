#include "lobby_directory.h"
#include "lobby_record.h"
#include <algorithm>
#include <set>

namespace redbaron::eos {
namespace {
void Release(std::vector<LobbyEntry>& entries) {
  for(auto& entry:entries)if(entry.details)EOS_LobbyDetails_Release(entry.details);
  entries.clear();
}
}
LobbyDirectory::~LobbyDirectory(){Release(entries_);}
void LobbyDirectory::Clear(){Release(entries_);}
EOS_HLobbyDetails LobbyDirectory::TakeDetails(const std::string& id) {
  auto it=std::find_if(entries_.begin(),entries_.end(),[&](const auto& e){return e.id==id;});
  if(it==entries_.end())return nullptr;
  auto handle=it->details;it->details=nullptr;entries_.erase(it);return handle;
}
bool LobbyDirectory::Refresh(EOS_HLobbySearch search) {
  if(!search)return false;
  EOS_LobbySearch_GetSearchResultCountOptions count_options{};
  count_options.ApiVersion=EOS_LOBBYSEARCH_GETSEARCHRESULTCOUNT_API_LATEST;
  const auto count=EOS_LobbySearch_GetSearchResultCount(search,&count_options);
  if(count>EOS_LOBBY_MAX_SEARCH_RESULTS)return false;
  std::vector<LobbyEntry> next;std::set<std::string> ids;
  for(uint32_t i=0;i<count && next.size()<32;++i) {
    EOS_LobbySearch_CopySearchResultByIndexOptions copy{};
    copy.ApiVersion=EOS_LOBBYSEARCH_COPYSEARCHRESULTBYINDEX_API_LATEST;copy.LobbyIndex=i;
    EOS_HLobbyDetails details{};
    if(EOS_LobbySearch_CopySearchResultByIndex(search,&copy,&details)!=EOS_EResult::EOS_Success)continue;
    EOS_LobbyDetails_CopyInfoOptions options{};options.ApiVersion=EOS_LOBBYDETAILS_COPYINFO_API_LATEST;
    EOS_LobbyDetails_Info* info{};
    if(EOS_LobbyDetails_CopyInfo(details,&options,&info)!=EOS_EResult::EOS_Success) {EOS_LobbyDetails_Release(details);continue;}
    auto record=ReadLobbyRecord(details);
    const bool valid=info && info->LobbyId && *info->LobbyId && EOS_ProductUserId_IsValid(info->LobbyOwnerUserId) &&
      info->PermissionLevel==EOS_ELobbyPermissionLevel::EOS_LPL_PUBLICADVERTISED && info->AvailableSlots>0 && record &&
      ids.insert(info->LobbyId).second;
    if(valid)next.push_back({info->LobbyId,std::move(*record),info->LobbyOwnerUserId,details});
    else EOS_LobbyDetails_Release(details);
    if(info)EOS_LobbyDetails_Info_Release(info);
  }
  Release(entries_);entries_=std::move(next);return true;
}
}
