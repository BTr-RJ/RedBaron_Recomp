#include "social.h"
#include <utility>
namespace redbaron::eos {
Social::Social(Platform& platform,std::function<void(std::string)> log,
               std::function<void(EOS_HLobbyDetails,EOS_UI_EventId)> accepted)
    :platform_(platform),log_(std::move(log)),accepted_(std::move(accepted)) {
  ui_=EOS_Platform_GetUIInterface(platform.handle());friends_=EOS_Platform_GetFriendsInterface(platform.handle());
  presence_=EOS_Platform_GetPresenceInterface(platform.handle());lobby_=EOS_Platform_GetLobbyInterface(platform.handle());
  EOS_Lobby_AddNotifyLobbyInviteAcceptedOptions invite{};invite.ApiVersion=EOS_LOBBY_ADDNOTIFYLOBBYINVITEACCEPTED_API_LATEST;
  invite_notify_=EOS_Lobby_AddNotifyLobbyInviteAccepted(lobby_,&invite,this,InviteAccepted);
  EOS_Lobby_AddNotifyJoinLobbyAcceptedOptions join{};join.ApiVersion=EOS_LOBBY_ADDNOTIFYJOINLOBBYACCEPTED_API_LATEST;
  join_notify_=EOS_Lobby_AddNotifyJoinLobbyAccepted(lobby_,&join,this,JoinAccepted);
  if(!platform.epic_user())return;
  EOS_UI_AddNotifyDisplaySettingsUpdatedOptions display{};display.ApiVersion=EOS_UI_ADDNOTIFYDISPLAYSETTINGSUPDATED_API_LATEST;
  display_notify_=EOS_UI_AddNotifyDisplaySettingsUpdated(ui_,&display,this,DisplayChanged);
  EOS_Friends_AddNotifyFriendsUpdateOptions updates{};updates.ApiVersion=EOS_FRIENDS_ADDNOTIFYFRIENDSUPDATE_API_LATEST;
  friends_notify_=EOS_Friends_AddNotifyFriendsUpdate(friends_,&updates,this,FriendsChanged);
  EOS_Friends_QueryFriendsOptions query{};query.ApiVersion=EOS_FRIENDS_QUERYFRIENDS_API_LATEST;query.LocalUserId=platform.epic_user();
  EOS_Friends_QueryFriends(friends_,&query,this,FriendsQueried);
  Presence("In menus");
}
Social::~Social() {Detach();}
void Social::Detach() {
  if(invite_notify_!=EOS_INVALID_NOTIFICATIONID)EOS_Lobby_RemoveNotifyLobbyInviteAccepted(lobby_,invite_notify_);
  if(join_notify_!=EOS_INVALID_NOTIFICATIONID)EOS_Lobby_RemoveNotifyJoinLobbyAccepted(lobby_,join_notify_);
  if(friends_notify_!=EOS_INVALID_NOTIFICATIONID)EOS_Friends_RemoveNotifyFriendsUpdate(friends_,friends_notify_);
  if(display_notify_!=EOS_INVALID_NOTIFICATIONID)EOS_UI_RemoveNotifyDisplaySettingsUpdated(ui_,display_notify_);
  invite_notify_=join_notify_=friends_notify_=display_notify_=EOS_INVALID_NOTIFICATIONID;
}
bool Social::ShowFriends() {
  if(!platform_.epic_user()) {log_("Epic friends require Epic sign-in; restart the game from the updated launcher");return false;}
  EOS_UI_ShowFriendsOptions show{};show.ApiVersion=EOS_UI_SHOWFRIENDS_API_LATEST;show.LocalUserId=platform_.epic_user();
  EOS_UI_ShowFriends(ui_,&show,this,FriendsShown);return true;
}
void EOS_CALL Social::FriendsQueried(const EOS_Friends_QueryFriendsCallbackInfo* data) {
  auto& s=*static_cast<Social*>(data->ClientData);
  if(data->ResultCode!=EOS_EResult::EOS_Success){s.log_("Epic friends query failed: "+std::string(EOS_EResult_ToString(data->ResultCode)));return;}
  EOS_Friends_GetFriendsCountOptions count{};count.ApiVersion=EOS_FRIENDS_GETFRIENDSCOUNT_API_LATEST;count.LocalUserId=s.platform_.epic_user();
  s.log_("Epic friends list ready: "+std::to_string(EOS_Friends_GetFriendsCount(s.friends_,&count))+" friend(s) visible to this application");
}
void EOS_CALL Social::FriendsChanged(const EOS_Friends_OnFriendsUpdateInfo* data) {
  static_cast<Social*>(data->ClientData)->log_("Epic friends list updated");
}
void EOS_CALL Social::DisplayChanged(const EOS_UI_OnDisplaySettingsUpdatedCallbackInfo* data) {
  static_cast<Social*>(data->ClientData)->captured_=data->bIsExclusiveInput==EOS_TRUE;
}
void EOS_CALL Social::FriendsShown(const EOS_UI_ShowFriendsCallbackInfo* data) {
  auto& s=*static_cast<Social*>(data->ClientData);
  s.log_(data->ResultCode==EOS_EResult::EOS_Success ? "Epic social overlay opened" :
    "Epic social overlay failed: "+std::string(EOS_EResult_ToString(data->ResultCode)));
}
void EOS_CALL Social::InviteAccepted(const EOS_Lobby_LobbyInviteAcceptedCallbackInfo* data) {
  auto& s=*static_cast<Social*>(data->ClientData);if(data->LocalUserId!=s.platform_.user())return;
  EOS_Lobby_CopyLobbyDetailsHandleByInviteIdOptions copy{};copy.ApiVersion=EOS_LOBBY_COPYLOBBYDETAILSHANDLEBYINVITEID_API_LATEST;copy.InviteId=data->InviteId;
  EOS_HLobbyDetails details{};const auto result=EOS_Lobby_CopyLobbyDetailsHandleByInviteId(s.lobby_,&copy,&details);
  if(result==EOS_EResult::EOS_Success && details)s.accepted_(details,EOS_UI_EVENTID_INVALID);
  else s.log_("Accepted Epic invite could not be resolved: "+std::string(EOS_EResult_ToString(result)));
}
void EOS_CALL Social::JoinAccepted(const EOS_Lobby_JoinLobbyAcceptedCallbackInfo* data) {
  auto& s=*static_cast<Social*>(data->ClientData);if(data->LocalUserId!=s.platform_.user())return;
  EOS_Lobby_CopyLobbyDetailsHandleByUiEventIdOptions copy{};copy.ApiVersion=EOS_LOBBY_COPYLOBBYDETAILSHANDLEBYUIEVENTID_API_LATEST;copy.UiEventId=data->UiEventId;
  EOS_HLobbyDetails details{};const auto result=EOS_Lobby_CopyLobbyDetailsHandleByUiEventId(s.lobby_,&copy,&details);
  if(result==EOS_EResult::EOS_Success && details)s.accepted_(details,data->UiEventId);
  else {s.Acknowledge(data->UiEventId,result);s.log_("Epic join request could not be resolved");}
}
void Social::Acknowledge(EOS_UI_EventId id,EOS_EResult result) {
  if(id==EOS_UI_EVENTID_INVALID)return;
  EOS_UI_AcknowledgeEventIdOptions ack{};ack.ApiVersion=EOS_UI_ACKNOWLEDGEEVENTID_API_LATEST;ack.UiEventId=id;ack.Result=result;
  EOS_UI_AcknowledgeEventId(ui_,&ack);
}
void Social::Presence(const std::string& text) {
  if(!platform_.epic_user() || presence_busy_ || text==presence_text_ || std::chrono::steady_clock::now()<next_presence_)return;
  next_presence_=std::chrono::steady_clock::now()+std::chrono::seconds(30);
  EOS_Presence_CreatePresenceModificationOptions create{};create.ApiVersion=EOS_PRESENCE_CREATEPRESENCEMODIFICATION_API_LATEST;create.LocalUserId=platform_.epic_user();
  EOS_HPresenceModification handle{};if(EOS_Presence_CreatePresenceModification(presence_,&create,&handle)!=EOS_EResult::EOS_Success)return;
  EOS_PresenceModification_SetStatusOptions status{};status.ApiVersion=EOS_PRESENCEMODIFICATION_SETSTATUS_API_LATEST;status.Status=EOS_Presence_EStatus::EOS_PS_Online;
  EOS_PresenceModification_SetStatus(handle,&status);
  EOS_PresenceModification_SetRawRichTextOptions rich{};rich.ApiVersion=EOS_PRESENCEMODIFICATION_SETRAWRICHTEXT_API_LATEST;rich.RichText=text.c_str();
  EOS_PresenceModification_SetRawRichText(handle,&rich);
  EOS_Presence_SetPresenceOptions set{};set.ApiVersion=EOS_PRESENCE_SETPRESENCE_API_LATEST;set.LocalUserId=platform_.epic_user();set.PresenceModificationHandle=handle;
  presence_busy_=true;presence_text_=text;EOS_Presence_SetPresence(presence_,&set,this,PresenceSet);EOS_PresenceModification_Release(handle);
}
void EOS_CALL Social::PresenceSet(const EOS_Presence_SetPresenceCallbackInfo* data) {
  auto& s=*static_cast<Social*>(data->ClientData);s.presence_busy_=false;
  if(data->ResultCode!=EOS_EResult::EOS_Success){s.presence_text_.clear();s.log_("Epic presence update failed: "+std::string(EOS_EResult_ToString(data->ResultCode)));}
}
}
