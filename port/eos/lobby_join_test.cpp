#include "lobby_join.h"
#include <eos_init.h>
#include <iostream>
#include <stdexcept>
using namespace redbaron::eos;
static void* context{};
static EOS_Lobby_OnJoinLobbyCallback join_callback{};
static EOS_Lobby_OnLeaveLobbyCallback leave_callback{};
static int released=0;
static void Check(bool ok){if(!ok)throw std::runtime_error("Lobby join state test failed");}
static void EOS_CALL Join(EOS_HLobby,const EOS_Lobby_JoinLobbyOptions* o,void* data,EOS_Lobby_OnJoinLobbyCallback cb) {
  Check(o->LobbyDetailsHandle && o->LocalUserId);context=data;join_callback=cb;
}
static void EOS_CALL Leave(EOS_HLobby,const EOS_Lobby_LeaveLobbyOptions* o,void* data,EOS_Lobby_OnLeaveLobbyCallback cb) {
  Check(o->LobbyId && o->LocalUserId);context=data;leave_callback=cb;
}
static void EOS_CALL Release(EOS_HLobbyDetails){++released;}
int main() {
  try {
    EOS_InitializeOptions init{};init.ApiVersion=EOS_INITIALIZE_API_LATEST;init.ProductName="RedBaronTest";init.ProductVersion="1";
    Check(EOS_Initialize(&init)==EOS_EResult::EOS_Success);
    auto user=EOS_ProductUserId_FromString("00000000000000000000000000000001");
    {
      LobbyJoin client(reinterpret_cast<EOS_HLobby>(1),user,{Join,Leave,Release});
      auto details=reinterpret_cast<EOS_HLobbyDetails>(1);
      Check(client.Start("test-lobby",details));Check(client.state()==LobbyJoin::State::Joining);
      Check(!client.Start("other",details) && !client.Leave());
      EOS_Lobby_JoinLobbyCallbackInfo joined{};joined.ClientData=context;joined.ResultCode=EOS_EResult::EOS_Success;joined.LobbyId="test-lobby";
      join_callback(&joined);Check(client.state()==LobbyJoin::State::Joined && released==1);
      Check(client.Leave() && client.state()==LobbyJoin::State::Leaving);
      EOS_Lobby_LeaveLobbyCallbackInfo left{};left.ClientData=context;left.ResultCode=EOS_EResult::EOS_NoConnection;leave_callback(&left);
      Check(client.state()==LobbyJoin::State::Joined && client.Leave());
      left.ClientData=context;left.ResultCode=EOS_EResult::EOS_Success;leave_callback(&left);
      Check(client.state()==LobbyJoin::State::Idle && client.lobby_id().empty());
      Check(client.Start("test-lobby",details));joined.ClientData=context;joined.ResultCode=EOS_EResult::EOS_AccessDenied;
      join_callback(&joined);Check(client.state()==LobbyJoin::State::Failed && released==2);
      Check(client.Reset() && client.state()==LobbyJoin::State::Idle);
    }
    Check(EOS_Shutdown()==EOS_EResult::EOS_Success);
    std::cout<<"PASS: asynchronous lobby join, leave, failure and handle ownership\n";return 0;
  }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
