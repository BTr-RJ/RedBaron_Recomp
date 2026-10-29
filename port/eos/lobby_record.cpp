#include "lobby_record.h"
#include "session_record.h"
#include "../network/session_record.h"
#include <stdexcept>
#include <vector>

namespace redbaron::eos {
namespace {
constexpr size_t kMaxChunks=((kSessionMaxBytes*8+5)/6+kSessionChunkSize-1)/kSessionChunkSize;
constexpr const char* kCount="RB_RECORD_COUNT";
std::string Key(size_t i) {return "RB_RECORD_"+std::to_string(i);}
}
EOS_EResult PublishLobbyRecord(EOS_HLobby lobby,EOS_ProductUserId owner,const char* lobby_id,
                              std::string_view record,size_t old_count,void* context,
                              EOS_Lobby_OnUpdateLobbyCallback callback) {
  if (!lobby || !EOS_ProductUserId_IsValid(owner) || !lobby_id || !*lobby_id || !callback || old_count>kMaxChunks)
    return EOS_EResult::EOS_InvalidParameters;
  if (!redbaron::net::ValidSessionRecord(record)) return EOS_EResult::EOS_InvalidParameters;
  std::vector<std::string> chunks;
  try {chunks=EncodeSessionRecord(record);} catch(const std::invalid_argument&) {return EOS_EResult::EOS_InvalidParameters;}
  EOS_Lobby_UpdateLobbyModificationOptions modify{};
  modify.ApiVersion=EOS_LOBBY_UPDATELOBBYMODIFICATION_API_LATEST;modify.LocalUserId=owner;modify.LobbyId=lobby_id;
  EOS_HLobbyModification builder{};
  auto result=EOS_Lobby_UpdateLobbyModification(lobby,&modify,&builder);
  if(result!=EOS_EResult::EOS_Success)return result;
  EOS_Lobby_AttributeData attr{};
  attr.ApiVersion=EOS_LOBBY_ATTRIBUTEDATA_API_LATEST;attr.Key=kCount;
  attr.ValueType=EOS_ELobbyAttributeType::EOS_AT_INT64;attr.Value.AsInt64=static_cast<int64_t>(chunks.size());
  EOS_LobbyModification_AddAttributeOptions add{};
  add.ApiVersion=EOS_LOBBYMODIFICATION_ADDATTRIBUTE_API_LATEST;add.Attribute=&attr;
  add.Visibility=EOS_ELobbyAttributeVisibility::EOS_LAT_PUBLIC;
  result=EOS_LobbyModification_AddAttribute(builder,&add);
  for(size_t i=0;result==EOS_EResult::EOS_Success && i<chunks.size();++i) {
    const auto key=Key(i);attr.Key=key.c_str();attr.ValueType=EOS_ELobbyAttributeType::EOS_AT_STRING;
    attr.Value.AsUtf8=chunks[i].c_str();result=EOS_LobbyModification_AddAttribute(builder,&add);
  }
  for(size_t i=chunks.size();result==EOS_EResult::EOS_Success && i<old_count;++i) {
    const auto key=Key(i);EOS_LobbyModification_RemoveAttributeOptions remove{};
    remove.ApiVersion=EOS_LOBBYMODIFICATION_REMOVEATTRIBUTE_API_LATEST;remove.Key=key.c_str();
    result=EOS_LobbyModification_RemoveAttribute(builder,&remove);
  }
  if(result==EOS_EResult::EOS_Success) {
    EOS_Lobby_UpdateLobbyOptions update{};update.ApiVersion=EOS_LOBBY_UPDATELOBBY_API_LATEST;
    update.LobbyModificationHandle=builder;EOS_Lobby_UpdateLobby(lobby,&update,context,callback);
  }
  EOS_LobbyModification_Release(builder);return result;
}
std::optional<std::string> ReadLobbyRecord(EOS_HLobbyDetails details) {
  if (!details)return std::nullopt;
  EOS_LobbyDetails_CopyAttributeByKeyOptions options{};
  options.ApiVersion=EOS_LOBBYDETAILS_COPYATTRIBUTEBYKEY_API_LATEST;options.AttrKey=kCount;
  EOS_Lobby_Attribute* attr{};
  if(EOS_LobbyDetails_CopyAttributeByKey(details,&options,&attr)!=EOS_EResult::EOS_Success)return std::nullopt;
  int64_t count=0;
  if(attr && attr->Data && attr->Data->ValueType==EOS_ELobbyAttributeType::EOS_AT_INT64) count=attr->Data->Value.AsInt64;
  if(attr) EOS_Lobby_Attribute_Release(attr);
  if(count<=0 || count>static_cast<int64_t>(kMaxChunks))return std::nullopt;
  std::vector<std::string> chunks;chunks.reserve(static_cast<size_t>(count));
  for(int64_t i=0;i<count;++i) {
    const auto key=Key(static_cast<size_t>(i));options.AttrKey=key.c_str();attr=nullptr;
    if(EOS_LobbyDetails_CopyAttributeByKey(details,&options,&attr)!=EOS_EResult::EOS_Success)return std::nullopt;
    bool valid=attr && attr->Data && attr->Data->ValueType==EOS_ELobbyAttributeType::EOS_AT_STRING && attr->Data->Value.AsUtf8;
    if(valid) {
      const auto* value=attr->Data->Value.AsUtf8;size_t length=0;
      while(length<=kSessionChunkSize && value[length])++length;
      valid=length>0 && length<=kSessionChunkSize;
      if(valid) chunks.emplace_back(value,length);
    }
    if(attr) EOS_Lobby_Attribute_Release(attr);
    if(!valid)return std::nullopt;
  }
  auto record=DecodeSessionRecord(chunks);
  if(!record || !redbaron::net::ValidSessionRecord(*record))return std::nullopt;
  return record;
}
}
