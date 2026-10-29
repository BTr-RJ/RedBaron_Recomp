#include "lobby_roster.h"
#include "eos_wire.h"
#include <algorithm>
#include <array>
#include <vector>
namespace redbaron::eos {
namespace {
std::optional<std::string> Id(EOS_ProductUserId user) {
  if(!EOS_ProductUserId_IsValid(user))return std::nullopt;
  std::array<char,EOS_PRODUCTUSERID_MAX_LENGTH+1> buffer{};int32_t size=int32_t(buffer.size());
  if(EOS_ProductUserId_ToString(user,buffer.data(),&size)!=EOS_EResult::EOS_Success)return std::nullopt;
  return std::string(buffer.data());
}
struct Members {std::string owner;std::vector<std::string> members;};
std::optional<Members> GetMembers(EOS_HLobbyDetails details) {
  if(!details)return std::nullopt;
  EOS_LobbyDetails_GetLobbyOwnerOptions owner_options{};owner_options.ApiVersion=EOS_LOBBYDETAILS_GETLOBBYOWNER_API_LATEST;
  auto owner=Id(EOS_LobbyDetails_GetLobbyOwner(details,&owner_options));if(!owner)return std::nullopt;
  EOS_LobbyDetails_GetMemberCountOptions count_options{};count_options.ApiVersion=EOS_LOBBYDETAILS_GETMEMBERCOUNT_API_LATEST;
  const auto count=EOS_LobbyDetails_GetMemberCount(details,&count_options);if(!count || count>8)return std::nullopt;
  Members result{*owner,{}};
  for(uint32_t i=0;i<count;++i) {
    EOS_LobbyDetails_GetMemberByIndexOptions options{};options.ApiVersion=EOS_LOBBYDETAILS_GETMEMBERBYINDEX_API_LATEST;options.MemberIndex=i;
    auto member=Id(EOS_LobbyDetails_GetMemberByIndex(details,&options));if(!member)return std::nullopt;
    result.members.push_back(*member);
  }
  return result;
}
}
std::optional<Roster> AssignLobbyRoster(EOS_HLobbyDetails details,const Roster& previous) {
  auto data=GetMembers(details);if(!data)return std::nullopt;
  return AssignRoster(previous,data->owner,data->members);
}
std::optional<Roster> ReadLobbyRoster(EOS_HLobbyDetails details) {
  auto data=GetMembers(details);if(!data)return std::nullopt;
  std::array<std::string,2> encoded;
  for(size_t i=0;i<2;++i) {
    const auto key="RB_ROSTER_"+std::to_string(i);
    EOS_LobbyDetails_CopyAttributeByKeyOptions options{};options.ApiVersion=EOS_LOBBYDETAILS_COPYATTRIBUTEBYKEY_API_LATEST;options.AttrKey=key.c_str();
    EOS_Lobby_Attribute* attr{};
    if(EOS_LobbyDetails_CopyAttributeByKey(details,&options,&attr)!=EOS_EResult::EOS_Success)return std::nullopt;
    bool valid=attr && attr->Data && attr->Data->ValueType==EOS_ELobbyAttributeType::EOS_AT_STRING && attr->Data->Value.AsUtf8;
    if(valid) {
      const char* value=attr->Data->Value.AsUtf8;size_t n=0;while(n<=131 && value[n])++n;
      valid=n && n<=131;if(valid)encoded[i].assign(value,n);
    }
    if(attr)EOS_Lobby_Attribute_Release(attr);if(!valid)return std::nullopt;
  }
  return DecodeRoster(encoded,data->owner,data->members);
}
bool ApplyLobbyRoster(EOS_HLobbyDetails details,EosWire& wire) {
  auto roster=ReadLobbyRoster(details);if(!roster)return false;
  EOS_LobbyDetails_GetMemberCountOptions count{};count.ApiVersion=EOS_LOBBYDETAILS_GETMEMBERCOUNT_API_LATEST;
  const auto member_count=EOS_LobbyDetails_GetMemberCount(details,&count);
  std::array<EOS_ProductUserId,8> users{};
  for(uint32_t i=0;i<member_count;++i) {
    EOS_LobbyDetails_GetMemberByIndexOptions options{};options.ApiVersion=EOS_LOBBYDETAILS_GETMEMBERBYINDEX_API_LATEST;options.MemberIndex=i;
    auto user=EOS_LobbyDetails_GetMemberByIndex(details,&options);auto text=Id(user);if(!text)return false;
    const auto slot=std::find(roster->begin(),roster->end(),*text);
    if(slot==roster->end())return false;
    users[static_cast<size_t>(slot-roster->begin())]=user;
  }
  return wire.SetRoster(users);
}
EOS_EResult PublishLobbyRoster(EOS_HLobby lobby,EOS_ProductUserId owner,const char* id,EOS_HLobbyDetails details,const Roster& roster,
                              void* context,EOS_Lobby_OnUpdateLobbyCallback callback) {
  if(!lobby || !EOS_ProductUserId_IsValid(owner) || !id || !*id || !callback)return EOS_EResult::EOS_InvalidParameters;
  std::array<std::string,2> encoded;
  try {encoded=EncodeRoster(roster);}catch(...){return EOS_EResult::EOS_InvalidParameters;}
  auto owner_id=Id(owner);if(!owner_id || roster[0]!=*owner_id)return EOS_EResult::EOS_InvalidParameters;
  auto checked=AssignLobbyRoster(details,roster);if(!checked || *checked!=roster)return EOS_EResult::EOS_InvalidParameters;
  EOS_Lobby_UpdateLobbyModificationOptions modify{};modify.ApiVersion=EOS_LOBBY_UPDATELOBBYMODIFICATION_API_LATEST;
  modify.LocalUserId=owner;modify.LobbyId=id;EOS_HLobbyModification builder{};
  auto result=EOS_Lobby_UpdateLobbyModification(lobby,&modify,&builder);if(result!=EOS_EResult::EOS_Success)return result;
  for(size_t i=0;i<2 && result==EOS_EResult::EOS_Success;++i) {
    const auto key="RB_ROSTER_"+std::to_string(i);EOS_Lobby_AttributeData attr{};
    attr.ApiVersion=EOS_LOBBY_ATTRIBUTEDATA_API_LATEST;attr.Key=key.c_str();
    attr.ValueType=EOS_ELobbyAttributeType::EOS_AT_STRING;attr.Value.AsUtf8=encoded[i].c_str();
    EOS_LobbyModification_AddAttributeOptions add{};add.ApiVersion=EOS_LOBBYMODIFICATION_ADDATTRIBUTE_API_LATEST;
    add.Attribute=&attr;add.Visibility=EOS_ELobbyAttributeVisibility::EOS_LAT_PRIVATE;
    result=EOS_LobbyModification_AddAttribute(builder,&add);
  }
  if(result==EOS_EResult::EOS_Success){EOS_Lobby_UpdateLobbyOptions update{};update.ApiVersion=EOS_LOBBY_UPDATELOBBY_API_LATEST;
    update.LobbyModificationHandle=builder;EOS_Lobby_UpdateLobby(lobby,&update,context,callback);}
  EOS_LobbyModification_Release(builder);return result;
}
}
