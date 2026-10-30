#include <Windows.h>
#include "online_service.h"
#include "platform.h"
#include "address_wire.h"
#include "eos_wire.h"
#include "lobby_directory.h"
#include "lobby_join.h"
#include "lobby_publisher.h"
#include "lobby_roster.h"
#include "native_record.h"
#include "lobby_record.h"
#include "social.h"
#include "achievement_sync.h"
#include "matchmaking_policy.h"
#include "../network/socket_bridge.h"
#include <eos_lobby.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <future>
#include <map>
#include <mutex>
#include <random>
#include <thread>

namespace redbaron::eos {
using namespace rex::system::xam;
using Clock=std::chrono::steady_clock;
namespace {
struct TickTimer {
  HANDLE handle=CreateWaitableTimerExW(nullptr,nullptr,CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,TIMER_ALL_ACCESS);
  TickTimer(){if(!handle)throw std::runtime_error("A high-resolution timer is required for online play");}
  ~TickTimer(){if(handle)CloseHandle(handle);}
  void Wait() {
    LARGE_INTEGER due{};due.QuadPart=-10000;
    if(!SetWaitableTimer(handle,&due,0,nullptr,nullptr,FALSE) || WaitForSingleObject(handle,100)!=WAIT_OBJECT_0)
      throw std::runtime_error("Online transport timer failed");
  }
};
LanSessionId SessionId(const LanRecord& r){LanSessionId id;std::copy_n(r.info.begin(),8,id.begin());return id;}
uint32_t HostAddress(const LanRecord& r) {
  return (uint32_t(r.info[8])<<24)|(uint32_t(r.info[9])<<16)|(uint32_t(r.info[10])<<8)|r.info[11];
}
std::string SocketName(std::string_view id) {
  uint64_t hash=14695981039346656037ull;for(unsigned char c:id){hash^=c;hash*=1099511628211ull;}
  return "RB"+std::to_string(hash);
}
}
struct OnlineService::Impl {
  enum class Kind {Search,Connect,Probe,Leave,Friends,Rank};
  struct Request {
    Kind kind;uint32_t address=0,title=0;LanSessionId session{};
    std::vector<LanQosTarget> targets;std::vector<LanQosResult> qos;std::vector<LanRecord> records;
    std::vector<std::vector<uint16_t>> samples;
    std::mutex mutex;std::condition_variable cv;bool done=false,ok=false,started=false;
    uint64_t search_generation=0;Clock::time_point deadline=Clock::now()+std::chrono::seconds(20);
    explicit Request(Kind k):kind(k){}
    void Finish(bool value) {std::lock_guard lock(mutex);ok=value;done=true;cv.notify_all();}
  };
  struct QosPending {std::weak_ptr<Request> request;size_t index;EOS_ProductUserId peer;LanSessionId session;Clock::time_point sent;};
  OnlineOptions options;std::promise<uint16_t> startup;std::jthread worker;
  std::atomic<bool> running{true},client{false};uint16_t local_address=0;
  std::mutex requests_mutex;std::deque<std::shared_ptr<Request>> incoming;
  std::vector<std::shared_ptr<Request>> requests;
  std::unique_ptr<Platform> platform;
  std::unique_ptr<Social> social;
  std::unique_ptr<AchievementSync> achievement_sync;
  std::atomic<bool> overlay_captured{false};
  EOS_HLobby lobby{};EOS_HP2P p2p{};EOS_HLobbySearch search{};
  EOS_NotificationId updates=EOS_INVALID_NOTIFICATIONID,members=EOS_INVALID_NOTIFICATIONID,qos_notify=EOS_INVALID_NOTIFICATIONID;
  EOS_P2P_SocketId qos_socket{};
  LobbyDirectory directory;
  std::unique_ptr<LobbyJoin> join;
  std::unique_ptr<LobbyPublisher> publisher;
  std::unique_ptr<EosWire> wire;
  std::unique_ptr<AddressWire> addressed;
  std::unique_ptr<redbaron::net::SocketBridge> bridge;
  std::optional<LobbyEntry> invitation;
  EOS_UI_EventId invitation_event=EOS_UI_EVENTID_INVALID;
  Clock::time_point invitation_deadline{};
  std::map<uint32_t,MatchPing> pings;
  std::shared_ptr<Request> directory_probe;
  Clock::time_point next_ping{};
  std::string room,pending_record;LanSessionId native_session{};
  bool owned=false,creating=false,destroying=false,searching=false,roster_busy=false,closing=false,host_ending=false;
  bool private_room=false;
  bool failed=false;uint64_t generation=0;
  uint32_t selected_address=0;bool cancel_join=false;
  Roster published_roster{},submitted_roster{};
  Clock::time_point next_search{},next_snapshot{},next_roster{},qos_window{};
  unsigned qos_requests=0;std::map<uint64_t,QosPending> pending_qos;
  std::map<EOS_ProductUserId,Clock::time_point> qos_peers;
  std::random_device random;
  explicit Impl(OnlineOptions value):options(std::move(value)) {
    if(options.name_space==0 || options.name_space==255 || options.bucket.empty() || options.bucket.size()>64)
      throw std::invalid_argument("Invalid online service options");
    auto ready=startup.get_future();worker=std::jthread([this](std::stop_token stop){Run(stop);});
    try {ready.get();}catch(...){worker.request_stop();worker.join();throw;}
  }
  ~Impl(){worker.request_stop();if(worker.joinable())worker.join();}
  void Log(const std::string& value){if(options.log)options.log(value);}
  bool Submit(const std::shared_ptr<Request>& r) {
    {std::lock_guard lock(requests_mutex);if(!running)return false;incoming.push_back(r);}
    std::unique_lock lock(r->mutex);r->cv.wait(lock,[&]{return r->done;});return r->ok;
  }
  static void EOS_CALL Found(const EOS_LobbySearch_FindCallbackInfo* data) {
    auto& s=*static_cast<Impl*>(data->ClientData);s.searching=false;++s.generation;
    if(data->ResultCode==EOS_EResult::EOS_Success && !s.closing) {
      s.directory.Refresh(s.search);
      s.next_ping={};
      s.Log("EOS directory refreshed: "+std::to_string(s.directory.entries().size())+" public session(s)");
    }
    else s.Log("EOS room search failed (code "+std::to_string(int(data->ResultCode))+")");
  }
  void Find() {
    if(searching || closing)return;
    if(!search) {
      EOS_Lobby_CreateLobbySearchOptions create{};create.ApiVersion=EOS_LOBBY_CREATELOBBYSEARCH_API_LATEST;create.MaxResults=32;
      if(EOS_Lobby_CreateLobbySearch(lobby,&create,&search)!=EOS_EResult::EOS_Success){failed=true;return;}
      EOS_Lobby_AttributeData attr{};attr.ApiVersion=EOS_LOBBY_ATTRIBUTEDATA_API_LATEST;
      attr.Key=EOS_LOBBY_SEARCH_BUCKET_ID;attr.ValueType=EOS_ELobbyAttributeType::EOS_AT_STRING;attr.Value.AsUtf8=options.bucket.c_str();
      EOS_LobbySearch_SetParameterOptions set{};set.ApiVersion=EOS_LOBBYSEARCH_SETPARAMETER_API_LATEST;
      set.Parameter=&attr;set.ComparisonOp=EOS_EComparisonOp::EOS_CO_EQUAL;
      if(EOS_LobbySearch_SetParameter(search,&set)!=EOS_EResult::EOS_Success){failed=true;return;}
    }
    searching=true;next_search=Clock::now()+std::chrono::seconds(10);
    EOS_LobbySearch_FindOptions find{};find.ApiVersion=EOS_LOBBYSEARCH_FIND_API_LATEST;find.LocalUserId=platform->user();
    EOS_LobbySearch_Find(search,&find,this,Found);
  }
  std::vector<LanRecord> Records(uint32_t title) {
    std::vector<LanRecord> out;std::map<uint32_t,unsigned> addresses;
    for(const auto& entry:directory.entries()) {
      if(entry.owner==platform->user())continue;
      auto record=DecodeNativeRecord(entry.record);if(!record || record->title!=title)continue;
      const uint32_t expected=0x0acb0000u|NetworkId(ProductUserString(entry.owner));
      if(HostAddress(*record)!=expected || uint16_t(expected)==local_address)continue;
      ++addresses[expected];out.push_back(std::move(*record));
    }
    std::erase_if(out,[&](const LanRecord& r){return addresses[HostAddress(r)]!=1;});return out;
  }
  const LobbyEntry* Entry(uint32_t address) {
    if(invitation) {
      const auto native=DecodeNativeRecord(invitation->record);
      if(native && HostAddress(*native)==address)return &*invitation;
    }
    const LobbyEntry* result=nullptr;
    for(const auto& entry:directory.entries()) {
      auto record=DecodeNativeRecord(entry.record);
      if(record && HostAddress(*record)==address && entry.owner!=platform->user() &&
         address==(0x0acb0000u|NetworkId(ProductUserString(entry.owner)))) {
        if(result)return nullptr;result=&entry;
      }
    }
    return result;
  }
  static void EOS_CALL Created(const EOS_Lobby_CreateLobbyCallbackInfo* data) {
    auto& s=*static_cast<Impl*>(data->ClientData);s.creating=false;
    if(data->ResultCode!=EOS_EResult::EOS_Success || !data->LobbyId){s.failed=true;s.Log("EOS room creation failed");return;}
    s.room=data->LobbyId;s.owned=true;
    s.publisher=std::make_unique<LobbyPublisher>(s.lobby,s.platform->user(),s.room);
    s.publisher->Offer(s.pending_record);s.next_roster={};s.Log(s.private_room?"EOS private room created from the native game session":"EOS public room created from the native game session");
  }
  void Snapshot() {
    if(client || closing || destroying)return;
    if(host_ending){if(owned)DestroyRoom();return;}
    const auto records=LanLocalSnapshot();
    if(records.empty()) {
      pending_record.clear();
      // A match may stop advertising while it is full or in progress. Keep
      // transport alive until the native session is actually deleted.
      if(owned && publisher)publisher->Offer("");return;
    }
    // A presence party is never advertised by the native runtime. There can
    // only be one active public gameplay session per game process.
    const auto& record=records.front();
    if(owned && SessionId(record)!=native_session){DestroyRoom();return;}
    pending_record=EncodeNativeRecord(record);
    if(owned){publisher->Offer(pending_record);return;}
    if(pending_record.empty())return;
    if(creating)return;
    native_session=SessionId(record);creating=true;
    private_room=record.private_session;
    EOS_Lobby_CreateLobbyOptions create{};create.ApiVersion=EOS_LOBBY_CREATELOBBY_API_LATEST;
    create.LocalUserId=platform->user();create.MaxLobbyMembers=std::clamp(record.open_public+record.filled_public+record.open_private+record.filled_private,2u,8u);
    create.PermissionLevel=record.private_session?EOS_ELobbyPermissionLevel::EOS_LPL_INVITEONLY:EOS_ELobbyPermissionLevel::EOS_LPL_PUBLICADVERTISED;
    create.bAllowInvites=EOS_TRUE;
    create.bPresenceEnabled=platform->epic_user()?EOS_TRUE:EOS_FALSE;
    create.bEnableJoinById=EOS_FALSE;
    create.bRejoinAfterKickRequiresInvite=EOS_TRUE;
    Log(record.private_session?"Creating an invite-only EOS room":"Creating a public EOS room");
    create.BucketId=options.bucket.c_str();create.bDisableHostMigration=EOS_TRUE;
    EOS_Lobby_CreateLobby(lobby,&create,this,Created);
  }
  static void EOS_CALL Destroyed(const EOS_Lobby_DestroyLobbyCallbackInfo* data) {
    auto& s=*static_cast<Impl*>(data->ClientData);s.destroying=false;
    if(data->ResultCode!=EOS_EResult::EOS_Success && data->ResultCode!=EOS_EResult::EOS_NotFound) {
      s.failed=true;s.Log("EOS room removal failed");return;
    }
    s.room.clear();s.owned=false;s.host_ending=false;s.published_roster={};s.Log("EOS room removed");
  }
  void DestroyRoom() {
    if(!owned || destroying || roster_busy || (publisher && !publisher->Synced()))return;
    ResetTransport();destroying=true;
    EOS_Lobby_DestroyLobbyOptions destroy{};destroy.ApiVersion=EOS_LOBBY_DESTROYLOBBY_API_LATEST;
    destroy.LocalUserId=platform->user();destroy.LobbyId=room.c_str();EOS_Lobby_DestroyLobby(lobby,&destroy,this,Destroyed);
  }
  void ResetTransport(){bridge.reset();addressed.reset();wire.reset();}
  void ClearInvitation(EOS_EResult result) {
    if(invitation && invitation->details)EOS_LobbyDetails_Release(invitation->details);
    invitation.reset();if(social)social->Acknowledge(invitation_event,result);invitation_event=EOS_UI_EVENTID_INVALID;
  }
  void AcceptInvitation(EOS_HLobbyDetails details,EOS_UI_EventId event) {
    EOS_LobbyDetails_CopyInfoOptions copy{};copy.ApiVersion=EOS_LOBBYDETAILS_COPYINFO_API_LATEST;
    EOS_LobbyDetails_Info* info{};const auto record=ReadLobbyRecord(details);
    const auto native=record?DecodeNativeRecord(*record):std::nullopt;
    if(EOS_LobbyDetails_CopyInfo(details,&copy,&info)!=EOS_EResult::EOS_Success || !info || !native ||
       !info->BucketId || options.bucket!=info->BucketId || !info->LobbyId || info->AvailableSlots==0 ||
       info->LobbyOwnerUserId==platform->user() || native->title!=0x584109B5 ||
       uint64_t(native->open_public)+native->open_private==0 ||
       HostAddress(*native)!=(0x0acb0000u|NetworkId(ProductUserString(info->LobbyOwnerUserId)))) {
      if(info)EOS_LobbyDetails_Info_Release(info);EOS_LobbyDetails_Release(details);
      social->Acknowledge(event,EOS_EResult::EOS_InvalidParameters);Log("Epic invitation rejected: incompatible, full or invalid room");return;
    }
    ClearInvitation(EOS_EResult::EOS_Canceled);
    invitation=LobbyEntry{info->LobbyId,*record,info->LobbyOwnerUserId,details};invitation_event=event;
    invitation_deadline=Clock::now()+std::chrono::minutes(2);EOS_LobbyDetails_Info_Release(info);
    if(options.invite_accepted)options.invite_accepted(*native);
    else {ClearInvitation(EOS_EResult::EOS_NotImplemented);return;}
    Log("Epic invitation accepted; handing the session to the native in-game join flow");
  }
  static void EOS_CALL Updated(const EOS_Lobby_UpdateLobbyCallbackInfo* data) {
    auto& s=*static_cast<Impl*>(data->ClientData);s.roster_busy=false;
    if(data->ResultCode==EOS_EResult::EOS_Success)s.published_roster=s.submitted_roster;
    else {s.failed=true;s.Log("EOS membership publication failed");}
    s.next_roster={};
  }
  static void EOS_CALL LobbyChanged(const EOS_Lobby_LobbyUpdateReceivedCallbackInfo* data) {
    auto& s=*static_cast<Impl*>(data->ClientData);if(data->LobbyId && s.room==data->LobbyId)s.next_roster={};
  }
  static void EOS_CALL MemberChanged(const EOS_Lobby_LobbyMemberStatusReceivedCallbackInfo* data) {
    auto& s=*static_cast<Impl*>(data->ClientData);
    if(!data->LobbyId || s.room!=data->LobbyId)return;
    s.Log("EOS membership status changed: "+std::to_string(int(data->CurrentStatus)));
    s.next_roster={};
    if(data->CurrentStatus!=EOS_ELobbyMemberStatus::EOS_LMS_JOINED) {
      // Revoke stale routes immediately, before the replacement roster arrives.
      if(s.addressed && s.wire && s.bridge) {
        const auto address=NetworkId(ProductUserString(data->TargetUserId));
        const auto slots=s.addressed->addresses();
        for(uint16_t i=0;i<slots.size();++i)if(slots[i]==address && address!=s.local_address) {
          s.bridge->Member(address,false);s.wire->RemoveMember(i);
          if(i==0 && s.client){s.cancel_join=true;s.Log("The room host disconnected; returning to discovery");}
        }
      }
      if(data->TargetUserId==s.platform->user() && s.client) {
        s.cancel_join=true;s.Log("EOS room membership ended");
      }
    }
  }
  void SyncRoster() {
    if(room.empty() || destroying || closing)return;
    // EOS modification handles contain snapshots. Concurrent host updates can
    // replace one another, losing either the session record or the roster.
    if(owned && publisher && !publisher->Synced())return;
    EOS_Lobby_CopyLobbyDetailsHandleOptions options{};options.ApiVersion=EOS_LOBBY_COPYLOBBYDETAILSHANDLE_API_LATEST;
    options.LocalUserId=platform->user();options.LobbyId=room.c_str();EOS_HLobbyDetails details{};
    if(EOS_Lobby_CopyLobbyDetailsHandle(lobby,&options,&details)!=EOS_EResult::EOS_Success)return;
    const auto roster=owned ? AssignLobbyRoster(details,published_roster) : ReadLobbyRoster(details);
    if(!roster){EOS_LobbyDetails_Release(details);return;}
    const auto own=ProductUserString(platform->user());const auto it=std::find(roster->begin(),roster->end(),own);
    if(it==roster->end()){EOS_LobbyDetails_Release(details);return;}
    if(owned && *roster!=published_roster && !roster_busy) {
      submitted_roster=*roster;roster_busy=true;
      if(PublishLobbyRoster(lobby,platform->user(),room.c_str(),details,*roster,this,Updated)!=EOS_EResult::EOS_Success){roster_busy=false;failed=true;}
    }
    // Publish first; the host and clients then consume the same validated table.
    if(owned && *roster!=published_roster){EOS_LobbyDetails_Release(details);return;}
    if(!wire) {
      wire=std::make_unique<EosWire>(p2p,platform->user(),uint16_t(it-roster->begin()),SocketName(room));
      addressed=std::make_unique<AddressWire>(*wire,*roster);
      bridge=std::make_unique<redbaron::net::SocketBridge>(*addressed,options_namespace());
      wire->packet=[this](uint16_t peer,const redbaron::net::Bytes& b,bool reliable){addressed->Receive(peer,b,reliable);};
      addressed->packet=[this](uint16_t peer,const redbaron::net::Bytes& b,bool reliable){bridge->Packet(peer,b,reliable);};
      Log("EOS gameplay socket bridge initialized");
    }
    const auto previous=addressed->addresses();
    if(!addressed->Update(*roster) || !ApplyLobbyRoster(details,*wire))failed=true;
    else {
      const auto current=addressed->addresses();
      for(auto address:previous)if(address && address!=local_address && std::find(current.begin(),current.end(),address)==current.end())bridge->Member(address,false);
      for(auto address:current)if(address && address!=local_address)bridge->Member(address,true);
    }
    EOS_LobbyDetails_Release(details);
  }
  uint8_t options_namespace() const{return options.name_space;}
  void StartJoin(const std::shared_ptr<Request>& r) {
    if(client && selected_address==r->address) {r->started=true;return;}
    if(owned || creating || destroying || !room.empty()) {
      if(invitation){if(owned || creating)host_ending=true;return;}
      r->Finish(false);return;
    }
    const auto* entry=Entry(r->address);if(!entry){r->Finish(false);return;}
    const auto id=entry->id;const auto record=DecodeNativeRecord(entry->record);
    if(!record){r->Finish(false);return;}
    const bool invited=invitation && invitation->id==id;
    auto details=invited?invitation->details:directory.TakeDetails(id);if(!details){r->Finish(false);return;}
    if(invited){invitation->details=nullptr;invitation.reset();}
    if(join->state()==LobbyJoin::State::Failed)join->Reset();
    if(!join->Start(id,details)){EOS_LobbyDetails_Release(details);ClearInvitation(EOS_EResult::EOS_UnexpectedError);r->Finish(false);return;}
    private_room=invited;
    native_session=SessionId(*record);selected_address=r->address;cancel_join=false;client=true;r->started=true;
    Log("Joining the room selected by the native game");
  }
  static void EOS_CALL QosConnection(const EOS_P2P_OnIncomingConnectionRequestInfo* data) {
    auto& s=*static_cast<Impl*>(data->ClientData);
    if(!s.owned || s.closing || !data->SocketId || std::strcmp(data->SocketId->SocketName,s.qos_socket.SocketName))return;
    if(s.qos_peers.size()>=64 && !s.qos_peers.contains(data->RemoteUserId))return;
    EOS_P2P_AcceptConnectionOptions accept{};accept.ApiVersion=EOS_P2P_ACCEPTCONNECTION_API_LATEST;
    accept.LocalUserId=s.platform->user();accept.RemoteUserId=data->RemoteUserId;accept.SocketId=&s.qos_socket;
    if(EOS_P2P_AcceptConnection(s.p2p,&accept)==EOS_EResult::EOS_Success)s.qos_peers[data->RemoteUserId]=Clock::now();
  }
  bool SendQos(EOS_ProductUserId peer,const std::vector<uint8_t>& packet) {
    EOS_P2P_SendPacketOptions send{};send.ApiVersion=EOS_P2P_SENDPACKET_API_LATEST;
    send.LocalUserId=platform->user();send.RemoteUserId=peer;send.SocketId=&qos_socket;send.Channel=2;
    send.Data=packet.data();send.DataLengthBytes=uint32_t(packet.size());send.bAllowDelayedDelivery=EOS_TRUE;
    send.Reliability=EOS_EPacketReliability::EOS_PR_ReliableUnordered;
    return EOS_P2P_SendPacket(p2p,&send)==EOS_EResult::EOS_Success;
  }
  void StartProbe(const std::shared_ptr<Request>& r) {
    r->started=true;r->deadline=Clock::now()+std::chrono::seconds(6);r->qos.resize(r->targets.size());
    r->samples.resize(r->targets.size());
    for(size_t i=0;i<r->targets.size();++i) {
      const auto* entry=Entry(r->targets[i].address);if(!entry)continue;
      const auto native=DecodeNativeRecord(entry->record);if(!native || SessionId(*native)!=r->targets[i].id)continue;
      uint64_t nonce;do{nonce=(uint64_t(random())<<32)|random();}while(pending_qos.contains(nonce));
      std::vector<uint8_t> packet{'R','B','Q','1'};
      for(int shift=56;shift>=0;shift-=8)packet.push_back(uint8_t(nonce>>shift));
      packet.insert(packet.end(),r->targets[i].id.begin(),r->targets[i].id.end());
      if(SendQos(entry->owner,packet)) {
        r->qos[i].sent=1;pending_qos.emplace(nonce,QosPending{r,i,entry->owner,r->targets[i].id,Clock::now()});
        qos_peers[entry->owner]=Clock::now();
      }
    }
  }
  void PollQos() {
    if(Clock::now()-qos_window>std::chrono::seconds(1)){qos_window=Clock::now();qos_requests=0;}
    uint8_t channel=2;
    for(int n=0;n<64;++n) {
      std::array<uint8_t,1170> packet{};EOS_ProductUserId remote{};EOS_P2P_SocketId socket{};uint8_t received_channel=0;uint32_t size=0;
      socket.ApiVersion=EOS_P2P_SOCKETID_API_LATEST;
      EOS_P2P_ReceivePacketOptions receive{};receive.ApiVersion=EOS_P2P_RECEIVEPACKET_API_LATEST;
      receive.LocalUserId=platform->user();receive.MaxDataSizeBytes=uint32_t(packet.size());receive.RequestedChannel=&channel;
      const auto result=EOS_P2P_ReceivePacket(p2p,&receive,&remote,&socket,&received_channel,packet.data(),&size);
      if(result==EOS_EResult::EOS_NotFound)break;
      if(result!=EOS_EResult::EOS_Success){failed=true;break;}
      if(size<20 || received_channel!=2 || std::strncmp(socket.SocketName,qos_socket.SocketName,sizeof(socket.SocketName)))continue;
      uint64_t nonce=0;for(size_t i=4;i<12;++i)nonce=(nonce<<8)|packet[i];
      LanSessionId session;std::copy_n(packet.begin()+12,8,session.begin());
      if(!std::memcmp(packet.data(),"RBQ1",4) && size==20 && owned && session==native_session && ++qos_requests<=64) {
        const auto qos=LanLocalQos(session);if(!qos.contacted)continue;
        std::vector<uint8_t> response(packet.begin(),packet.begin()+20);response[2]='R';
        response.push_back(qos.disabled?0:1);response.push_back(uint8_t(qos.data.size()>>8));response.push_back(uint8_t(qos.data.size()));
        response.insert(response.end(),qos.data.begin(),qos.data.end());SendQos(remote,response);
      } else if(!std::memcmp(packet.data(),"RBR1",4) && size>=23) {
        const auto found=pending_qos.find(nonce);if(found==pending_qos.end() || found->second.peer!=remote || found->second.session!=session)continue;
        const size_t count=(size_t(packet[21])<<8)|packet[22];if(count>1024 || size!=23+count || packet[20]>1)continue;
        if(auto request=found->second.request.lock();request && !request->done) {
          auto& out=request->qos[found->second.index];out.contacted=true;out.disabled=!packet[20];++out.received;
          out.data.assign(packet.begin()+23,packet.begin()+size);
          auto elapsed=std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now()-found->second.sent).count();
          auto& samples=request->samples[found->second.index];samples.push_back(uint16_t(std::clamp<int64_t>(elapsed,1,65535)));
          std::sort(samples.begin(),samples.end());out.minimum_ms=samples.front();out.median_ms=samples[samples.size()/2];
          if(samples.size()<3 && Clock::now()<request->deadline) {
            packet[2]='Q';std::vector<uint8_t> probe(packet.begin(),packet.begin()+20);
            if(SendQos(remote,probe)){++out.sent;found->second.sent=Clock::now();continue;}
          }
        }
        pending_qos.erase(found);
      }
    }
    std::erase_if(pending_qos,[](const auto& item){return item.second.request.expired() || Clock::now()-item.second.sent>std::chrono::seconds(7);});
    for(auto it=qos_peers.begin();it!=qos_peers.end();) {
      if(Clock::now()-it->second<std::chrono::seconds(15)){++it;continue;}
      EOS_P2P_CloseConnectionOptions close{};close.ApiVersion=EOS_P2P_CLOSECONNECTION_API_LATEST;
      close.LocalUserId=platform->user();close.RemoteUserId=it->first;close.SocketId=&qos_socket;
      EOS_P2P_CloseConnection(p2p,&close);it=qos_peers.erase(it);
    }
  }
  void Process() {
    {std::lock_guard lock(requests_mutex);while(!incoming.empty()){requests.push_back(incoming.front());incoming.pop_front();}}
    for(const auto& r:requests) {
      if(r->done)continue;
      if(r->kind==Kind::Friends) {r->Finish(social && social->ShowFriends());}
      else if(r->kind==Kind::Rank) {
        const auto now=Clock::now();
        if(!r->started) {
          for(const auto& record:r->records) {
            const auto found=pings.find(HostAddress(record));
            if(found==pings.end() || found->second.expires<=now || found->second.session!=SessionId(record))
              r->targets.push_back({HostAddress(record),SessionId(record)});
          }
          if(!r->targets.empty()){StartProbe(r);Log("Measuring candidate hosts before choosing a match");}
          else r->started=true;
        }
        const bool waiting=std::any_of(pending_qos.begin(),pending_qos.end(),[&](const auto& item){return item.second.request.lock()==r;});
        if(waiting && now<r->deadline)continue;
        for(size_t i=0;i<r->qos.size();++i)if(r->qos[i].contacted)
          pings[r->targets[i].address]={r->targets[i].id,r->qos[i].median_ms,now+std::chrono::seconds(30)};
        const auto count=r->records.size();
        if(const auto best=SelectBestMatch(r->records,pings,Clock::now()))
          Log("Matchmaking selected the lowest measured ping: "+std::to_string(*best)+" ms from "+std::to_string(count)+" compatible host(s)");
        else r->records.clear();
        r->Finish(true);
      } else if(r->kind==Kind::Search) {
        // XGI invokes search on the guest thread. Never freeze rendering while
        // EOS performs an Internet request; refresh independently and return
        // the most recent authenticated directory snapshot immediately.
        if(Clock::now()>=next_search)Find();
        r->records=Records(r->title);r->Finish(true);
      } else if(r->kind==Kind::Connect) {
        if(addressed && addressed->Ready(uint16_t(r->address))){Log("EOS room admission ready; native game socket may connect");r->Finish(true);continue;}
        if(!r->started)StartJoin(r);
        if(join->state()==LobbyJoin::State::Failed){r->Finish(false);client=false;}
      } else if(r->kind==Kind::Probe) {
        if(!r->started)StartProbe(r);
        const bool waiting=std::any_of(pending_qos.begin(),pending_qos.end(),[&](const auto& x){return x.second.request.lock()==r;});
        if(!waiting || Clock::now()>=r->deadline)r->Finish(true);
      } else {
        if((owned || creating) && native_session==r->session)host_ending=true;
        if(client && native_session==r->session) {
          cancel_join=true;ResetTransport();if(join->state()==LobbyJoin::State::Joined)join->Leave();
        }
        r->Finish(true);
      }
      if(!r->done && Clock::now()>=r->deadline){r->Finish(false);if(r->kind==Kind::Connect)cancel_join=true;}
    }
    std::erase_if(requests,[](const auto& r){return r->done;});
  }
  void Shutdown() {
    closing=true;
    if(platform) {
      const auto deadline=Clock::now()+std::chrono::seconds(8);
      if(join && join->state()==LobbyJoin::State::Joined)join->Leave();
      do {
        platform->Tick();if(publisher)publisher->Poll();if(owned)DestroyRoom();
        if(!creating && !destroying && !owned && (!join || (join->state()!=LobbyJoin::State::Joining && join->state()!=LobbyJoin::State::Leaving)))break;
        if(join && join->state()==LobbyJoin::State::Joined)join->Leave();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
      }while(Clock::now()<deadline);
      if(updates!=EOS_INVALID_NOTIFICATIONID)EOS_Lobby_RemoveNotifyLobbyUpdateReceived(lobby,updates);
      if(members!=EOS_INVALID_NOTIFICATIONID)EOS_Lobby_RemoveNotifyLobbyMemberStatusReceived(lobby,members);
      if(qos_notify!=EOS_INVALID_NOTIFICATIONID)EOS_P2P_RemoveNotifyPeerConnectionRequest(p2p,qos_notify);
      ClearInvitation(EOS_EResult::EOS_Canceled);
      if(social)social->Detach();
      ResetTransport();
      if(search) {EOS_LobbySearch_Release(search);search=nullptr;}
      // Join/publisher callback contexts outlive platform release.
      directory.Clear();platform->Release();social.reset();achievement_sync.reset();join.reset();publisher.reset();platform.reset();
    }
    {std::lock_guard lock(requests_mutex);running=false;for(auto& r:incoming)r->Finish(false);incoming.clear();}
    for(auto& r:requests)if(!r->done)r->Finish(false);requests.clear();
  }
  void Run(std::stop_token stop) {
    bool announced=false;
    try {
      platform=std::make_unique<Platform>(options.config,options.player_name,options.epic_account,[this](auto message){Log(message);});
      const auto deadline=Clock::now()+std::chrono::seconds(options.epic_account?300:45);
      while(!stop.stop_requested() && !platform->LoginDone() && Clock::now()<deadline) {
        platform->Tick();std::this_thread::sleep_for(std::chrono::milliseconds(5));
      }
      if(!platform->LoggedIn())throw std::runtime_error("EOS login failed or timed out: "+std::string(EOS_EResult_ToString(platform->result())));
      local_address=NetworkId(ProductUserString(platform->user()));
      lobby=EOS_Platform_GetLobbyInterface(platform->handle());p2p=EOS_Platform_GetP2PInterface(platform->handle());
      if(options.force_relay) {
        EOS_P2P_SetRelayControlOptions relay{};relay.ApiVersion=EOS_P2P_SETRELAYCONTROL_API_LATEST;
        relay.RelayControl=EOS_ERelayControl::EOS_RC_ForceRelays;
        if(EOS_P2P_SetRelayControl(p2p,&relay)!=EOS_EResult::EOS_Success)throw std::runtime_error("EOS relay configuration failed");
      }
      join=std::make_unique<LobbyJoin>(lobby,platform->user(),LobbyJoinApi{},bool(platform->epic_user()));
      social=std::make_unique<Social>(*platform,[this](auto message){Log(message);},[this](auto details,auto event){AcceptInvitation(details,event);});
      if(options.achievements)achievement_sync=std::make_unique<AchievementSync>(*platform,[this](auto message){Log(message);},options.achievements,options.restore_achievement);
      EOS_Lobby_AddNotifyLobbyUpdateReceivedOptions update{};update.ApiVersion=EOS_LOBBY_ADDNOTIFYLOBBYUPDATERECEIVED_API_LATEST;
      updates=EOS_Lobby_AddNotifyLobbyUpdateReceived(lobby,&update,this,LobbyChanged);
      EOS_Lobby_AddNotifyLobbyMemberStatusReceivedOptions member{};member.ApiVersion=EOS_LOBBY_ADDNOTIFYLOBBYMEMBERSTATUSRECEIVED_API_LATEST;
      members=EOS_Lobby_AddNotifyLobbyMemberStatusReceived(lobby,&member,this,MemberChanged);
      qos_socket.ApiVersion=EOS_P2P_SOCKETID_API_LATEST;std::memcpy(qos_socket.SocketName,"RedBaronQosV1",sizeof("RedBaronQosV1"));
      EOS_P2P_AddNotifyPeerConnectionRequestOptions notify{};notify.ApiVersion=EOS_P2P_ADDNOTIFYPEERCONNECTIONREQUEST_API_LATEST;
      notify.LocalUserId=platform->user();notify.SocketId=&qos_socket;
      qos_notify=EOS_P2P_AddNotifyPeerConnectionRequest(p2p,&notify,this,QosConnection);
      if(updates==EOS_INVALID_NOTIFICATIONID || members==EOS_INVALID_NOTIFICATIONID || qos_notify==EOS_INVALID_NOTIFICATIONID)
        throw std::runtime_error("EOS notification registration failed");
      TickTimer timer;
      startup.set_value(local_address);announced=true;Log("EOS authenticated; in-game public matchmaking enabled");
      while(!stop.stop_requested() && !failed) {
        platform->Tick();if(!platform->LoggedIn())throw std::runtime_error("EOS authentication expired");
        if(cancel_join && join->state()==LobbyJoin::State::Joined){ResetTransport();join->Leave();}
        if(join->state()==LobbyJoin::State::Joined && room.empty()){room=join->lobby_id();next_roster={};}
        if(client && join->state()==LobbyJoin::State::Idle){ResetTransport();room.clear();client=false;selected_address=0;cancel_join=false;}
        const auto now=Clock::now();
        overlay_captured=social->captured();
        if(achievement_sync)achievement_sync->Poll();
        if(invitation && now>=invitation_deadline)ClearInvitation(EOS_EResult::EOS_TimedOut);
        if(join->state()==LobbyJoin::State::Failed && invitation_event!=EOS_UI_EVENTID_INVALID) {
          social->Acknowledge(invitation_event,join->error());invitation_event=EOS_UI_EVENTID_INVALID;
        }
        if(join->state()==LobbyJoin::State::Joined && invitation_event!=EOS_UI_EVENTID_INVALID) {
          social->Acknowledge(invitation_event,EOS_EResult::EOS_Success);invitation_event=EOS_UI_EVENTID_INVALID;
        }
        if(directory_probe && (directory_probe->done || now>=directory_probe->deadline ||
           std::none_of(pending_qos.begin(),pending_qos.end(),[&](const auto& item){return item.second.request.lock()==directory_probe;}))) {
          for(size_t i=0;i<directory_probe->targets.size();++i)if(directory_probe->qos[i].contacted)
            pings[directory_probe->targets[i].address]={directory_probe->targets[i].id,directory_probe->qos[i].median_ms,now+std::chrono::seconds(30)};
          directory_probe.reset();
        }
        if(!directory_probe && !client && now>=next_ping) {
          next_ping=now+std::chrono::seconds(10);auto records=Records(0x584109B5);
          if(!records.empty()) {
            directory_probe=std::make_shared<Request>(Kind::Probe);
            for(const auto& r:records)directory_probe->targets.push_back({HostAddress(r),SessionId(r)});
            StartProbe(directory_probe);
          }
        }
        if(now>=next_snapshot){next_snapshot=now+std::chrono::seconds(1);Snapshot();}
        social->Presence(room.empty()?"In menus":private_room?"In a private match":"In a public match");
        if(now>=next_roster){next_roster=now+std::chrono::milliseconds(250);SyncRoster();}
        if(publisher && owned && !destroying && !roster_busy){publisher->Poll();if(publisher->error()!=EOS_EResult::EOS_Success)failed=true;}
        if(!client && now>=next_search)Find();
        Process();PollQos();
        if(wire && !wire->Poll())throw std::runtime_error("EOS reliable gameplay transport failed");
        if(bridge)bridge->Poll();
        timer.Wait();
      }
      if(failed)Log("EOS session stopped after a service error; restart online to reconnect");
    } catch(const std::exception& error) {
      if(!announced)startup.set_exception(std::current_exception());else Log(error.what());
    }
    Shutdown();
  }
};
OnlineService::OnlineService(OnlineOptions options):impl_(std::make_unique<Impl>(std::move(options))){}
OnlineService::~OnlineService()=default;
void OnlineService::Stop(){impl_->running=false;impl_->worker.request_stop();if(impl_->worker.joinable())impl_->worker.join();}
uint16_t OnlineService::network_id() const{return impl_->local_address;}
bool OnlineService::CanHost() const{return impl_->running && !impl_->client;}
std::vector<LanRecord> OnlineService::Search(uint32_t title) {
  auto r=std::make_shared<Impl::Request>(Impl::Kind::Search);r->title=title;
  return impl_->Submit(r) ? r->records : std::vector<LanRecord>{};
}
std::vector<LanQosResult> OnlineService::Probe(const std::vector<LanQosTarget>& targets,uint32_t probes) {
  if(targets.empty() || targets.size()>32 || !probes)return std::vector<LanQosResult>(targets.size());
  auto r=std::make_shared<Impl::Request>(Impl::Kind::Probe);r->targets=targets;
  return impl_->Submit(r) ? r->qos : std::vector<LanQosResult>(targets.size());
}
bool OnlineService::Connect(uint32_t address) {
  if((address&0xffff0000u)!=0x0acb0000u || uint16_t(address)==network_id())return true;
  auto r=std::make_shared<Impl::Request>(Impl::Kind::Connect);r->address=address;return impl_->Submit(r);
}
void OnlineService::Leave(const LanSessionId& session) {
  auto r=std::make_shared<Impl::Request>(Impl::Kind::Leave);r->session=session;impl_->Submit(r);
}
bool OnlineService::ShowFriends(uint32_t user) {
  if(user!=0)return false;return impl_->Submit(std::make_shared<Impl::Request>(Impl::Kind::Friends));
}
bool OnlineService::ShowAchievements(uint32_t user) {
  if(user || !impl_->options.show_achievements)return false;impl_->options.show_achievements();return true;
}
bool OnlineService::InputCaptured() const {return impl_->overlay_captured.load();}
void OnlineService::RankMatches(std::vector<LanRecord>& records) {
  if(records.size()<2)return;
  auto r=std::make_shared<Impl::Request>(Impl::Kind::Rank);r->records=records;
  if(impl_->Submit(r))records=std::move(r->records);
}
}
