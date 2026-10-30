// Authored runtime integration tests. No game binary or assets are loaded.
#include <rex/runtime.h>
#include <rex/logging.h>
#include <rex/system/xsession.h>
#include <rex/system/xsocket.h>
#include <rex/system/xam/user_profile.h>
#include <rex/system/xam/lan_lobby.h>
#include <rex/system/xam/stats_provider.h>
#include <rex/system/xenumerator.h>
#include <rex/system/util/object_table.h>
#include <WinSock2.h>
#include <iostream>
#include <stdexcept>

REXCVAR_DECLARE(uint32_t, redbaron_local_profiles);
REXCVAR_DECLARE(bool, redbaron_lan);
REXCVAR_DECLARE(std::string, redbaron_lan_bind);
REXCVAR_DECLARE(std::string, redbaron_lan_peer);
namespace rex::kernel::xam {
uint32_t XGetLanguage_entry();
uint32_t NetDll_accept_entry(uint32_t, uint32_t, ppc_ptr_t<system::XSOCKADDR>, mapped_u32);
}
static void Check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
class TestStats final:public rex::system::xam::StatsProvider {
 public:
 rex::system::xam::StatQuery last;std::vector<rex::system::xam::StatWrite> written;
 bool Write(uint32_t,uint64_t,const std::vector<rex::system::xam::StatWrite>& views) override {written=views;return true;}
 rex::system::xam::StatResult Read(const rex::system::xam::StatQuery& query) override {
  last=query;rex::system::xam::StatRow row;row.xuid=0x0009000012345600ull;row.rank=3;row.rating=9876543210ll;row.name="Test pilot";row.columns={{3,-7},{7,55}};return {true,9,{row}};
 }
};
int main(int argc, char** argv) {
  if (argc != 2) return 2;
  try {
    rex::InitLogging();
    REXCVAR_SET(redbaron_local_profiles, 4);
    REXCVAR_SET(redbaron_lan, true);
    REXCVAR_SET(redbaron_lan_bind, "127.0.0.2");
    REXCVAR_SET(redbaron_lan_peer, "127.0.0.2");
    rex::Runtime runtime({}, std::filesystem::absolute(argv[1]));
    rex::RuntimeConfig config; config.tool_mode = true;
    Check(runtime.Setup(std::move(config)) == 0, "Runtime setup");
    for(auto language:{1u,2u,3u,4u,5u,6u,9u}) {
      Check(rex::cvar::SetFlagByName("user_language",std::to_string(language)),"Runtime language preference accepted");
      Check(rex::kernel::xam::XGetLanguage_entry()==language,"Guest language getter respects the selected language");
    }
    rex::cvar::SetFlagByName("user_language","1");
    auto* kernel = runtime.kernel_state(); auto* mem = runtime.memory();
    for (unsigned i = 0; i < 4; ++i) {
      auto* profile = kernel->user_profile(i); Check(profile && profile->index() == i, "Four distinct profiles");
      Check(profile->xuid() == kernel->user_profile(0)->xuid() + i, "Distinct network IDs");
      Check((profile->xuid() >> 48) == 9, "Network identity satisfies XAM XUID format");
      Check(profile->storage_xuid() == 0xB13EBABEBABEBABEull + i, "Legacy save identity preserved");
    }
    Check(!kernel->user_profile(4), "Invalid profile rejected");
    rex::system::object_ref<rex::system::XSession> session(new rex::system::XSession(kernel));
    Check(session->Initialize() == 0, "Session native handle");
    const auto address = mem->SystemHeapAlloc(4096);
    auto* bytes = mem->TranslateVirtual(address);
    auto put = [&](unsigned offset, uint32_t value) { rex::memory::store_and_swap<uint32_t>(bytes + offset, value); };
    auto get = [&](unsigned offset) { return rex::memory::load_and_swap<uint32_t>(bytes + offset); };
    {
      using namespace rex::system;using namespace rex::system::xam;using rex::X_RESULT;
      auto provider=std::make_shared<TestStats>();SetStatsProvider(provider);
      put(2048,51);put(2052,2);rex::memory::store_and_swap<uint16_t>(bytes+2056,3);rex::memory::store_and_swap<uint16_t>(bytes+2058,7);
      Check(CreateStatsEnumerator(kernel,0x584109B5,1,2,10,1,address+2048,address+2200,address+2204)==0,"Leaderboard enumerator creation");
      Check(provider->last.start==2&&provider->last.count==10&&provider->last.columns==std::vector<uint16_t>({3,7}),"Native rank and column filters preserved");
      Check(get(2200)==120,"Leaderboard nested output buffer size");put(2424,0x12345678);
      auto enumerator=kernel->object_table()->LookupObject<XEnumerator>(get(2204));uint32_t written=0;
      Check(enumerator&&enumerator->WriteItems(address+2304,bytes+2304,&written)==0&&written==1,"Leaderboard enumeration writes one result container");
      Check(get(2304)==1&&get(2308)==address+2312&&get(2312)==51&&get(2316)==9&&get(2320)==1,"Leaderboard views preserve native pointers and totals");
      Check(get(2336)==3&&rex::memory::load_and_swap<uint64_t>(bytes+2344)==9876543210ull,"Leaderboard rank/rating alignment and endian");
      Check(get(2368)==2&&get(2372)==address+2376&&rex::memory::load_and_swap<uint64_t>(bytes+2392)==uint64_t(-7),"Leaderboard columns preserve signed values");
      Check(get(2424)==0x12345678,"Leaderboard output preserves trailing memory");
      Check(enumerator->WriteItems(address+2304,bytes+2304,&written)==X_ERROR_NO_MORE_FILES&&written==0,"Leaderboard end of enumeration");
      Check(CreateStatsEnumerator(kernel,0x584109B5,0,0x0009000012345600ull,10,1,address+2048,address+2200,address+2204)==0&&provider->last.pivot==0x0009000012345600ull,"Leaderboard XUID-centered query");
      put(2052,65);Check(CreateStatsEnumerator(kernel,0,1,1,10,1,address+2048,address+2200,address+2204)==X_ERROR_INVALID_PARAMETER,"Oversized leaderboard spec rejected");put(2052,2);
      put(2560,0);rex::memory::store_and_swap<uint64_t>(bytes+2564,0x0009000012345600ull);put(2572,1);put(2576,address+2600);
      put(2600,51);put(2604,2);put(2608,address+2640);put(2640,0x10000001);bytes[2648]=1;put(2656,uint32_t(-9));put(2664,0x20000002);bytes[2672]=2;rex::memory::store_and_swap<uint64_t>(bytes+2680,12345678901ull);
      Check(DispatchStats(kernel,0xB0025,address+2560,20)==0&&provider->written.size()==1&&provider->written[0].properties.at(0x10000001)==-9&&provider->written[0].properties.at(0x20000002)==12345678901ll,"Native stat writes retain 32/64-bit properties");
      Check(DispatchStats(kernel,0xB0025,address+2560,19)==X_ERROR_INVALID_PARAMETER,"Truncated stat write rejected");
      SetStatsProvider({});Check(CreateStatsEnumerator(kernel,0,1,1,10,1,address+2048,address+2200,address+2204)==X_ERROR_NOT_LOGGED_ON,"Missing leaderboard provider reports unavailable");
    }
    auto call = [&](uint32_t message, uint32_t size) { auto result = rex::system::XSession::Dispatch(kernel, message, address, size); Check(result.has_value(), "Session dispatcher"); return *result; };
    put(0, session->guest_object()); put(4, 1); put(8, 2); put(12, 0); put(16, 0); put(20, address + 128); put(24, address + 192);
    Check(call(0xB0010, 28) == 0, "Session create");
    Check(call(0xB0010, 28) != 0, "Duplicate create rejected");
    put(4, 2); put(8, 0); put(12, address + 256); put(16, address + 272);
    put(256, 0); put(260, 1); put(272, 0); put(276, 0);
    Check(call(0xB0012, 20) == 0, "Join two distinct profiles");
    put(4, 1); put(256, 2);
    Check(call(0xB0012, 20) != 0, "Full session rejects additional player atomically");
    put(4, 160); put(8, address + 320); put(480, 0x12345678);
    Check(call(0xB001D, 24) == 0 && get(352) == 2, "Details report actual member count");
    Check(get(480) == 0x12345678, "Details ABI bounds include two 16-byte members");
    put(4, 159); Check(call(0xB001D, 24) == 0x80155207, "Details rejects undersized buffer value");
    Check(call(0xB0014, 16) == 0 && call(0xB0014, 16) != 0, "Start state transition");
    Check(call(0xB0015, 16) == 0, "End state transition");
    Check(call(0xB0011, 16) == 0 && call(0xB0014, 16) != 0, "Deleted session rejected");

    // Real Winsock datagrams exercise guest/native address and port byte order.
    WSADATA wsa{}; Check(WSAStartup(MAKEWORD(2,2), &wsa) == 0, "Winsock setup");
    rex::system::object_ref<rex::system::XSocket> a(new rex::system::XSocket(kernel)), b(new rex::system::XSocket(kernel));
    Check(a->Initialize(rex::system::XSocket::X_AF_INET, rex::system::XSocket::X_SOCK_DGRAM, rex::system::XSocket::X_IPPROTO_UDP) == 0, "UDP sender");
    Check(b->Initialize(rex::system::XSocket::X_AF_INET, rex::system::XSocket::X_SOCK_DGRAM, rex::system::XSocket::X_IPPROTO_UDP) == 0, "UDP receiver");
    rex::system::N_XSOCKADDR_IN bind_address{}; bind_address.sin_family = AF_INET; bind_address.sin_addr = 0x7F000001; bind_address.sin_port = 0;
    Check(b->Bind(&bind_address, 16) == 0, "UDP bind");
    sockaddr_in actual{}; int length = sizeof(actual); getsockname(b->native_handle(), reinterpret_cast<sockaddr*>(&actual), &length);
    bind_address.sin_port = ntohs(actual.sin_port);
    DWORD timeout = 1000; setsockopt(b->native_handle(), SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<char*>(&timeout), sizeof(timeout));
    uint8_t payload[] = {3, 19, 77, 201}, received[16]{};
    Check(a->SendTo(payload, sizeof(payload), 0, &bind_address, 16) == sizeof(payload), "UDP send destination endian");
    rex::system::N_XSOCKADDR_IN source{}; uint32_t source_size = 16;
    Check(b->RecvFrom(received, sizeof(received), 0, &source, &source_size) == sizeof(payload), "UDP receive");
    Check(!std::memcmp(payload, received, sizeof(payload)) && uint32_t(source.sin_addr) == 0x7F000001, "UDP bytes/source endian");
    getsockname(a->native_handle(), reinterpret_cast<sockaddr*>(&actual), &length);
    Check(uint16_t(source.sin_port) == ntohs(actual.sin_port), "UDP source port endian");
    Check(a->Close() == 0 && a->Close() == 0, "Socket close is idempotent");

    rex::system::object_ref<rex::system::XSocket> listener(new rex::system::XSocket(kernel));
    Check(listener->Initialize(rex::system::XSocket::X_AF_INET, rex::system::XSocket::X_SOCK_STREAM, rex::system::XSocket::X_IPPROTO_TCP) == 0, "TCP listener");
    bind_address.sin_port = 0;
    Check(listener->Bind(&bind_address, 16) == 0 && listener->Listen(2) == 0, "TCP bind/listen");
    getsockname(listener->native_handle(), reinterpret_cast<sockaddr*>(&actual), &length);
    SOCKET client = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    Check(connect(client, reinterpret_cast<sockaddr*>(&actual), length) == 0, "TCP local connection");
    const auto accepted = rex::kernel::xam::NetDll_accept_entry(1, listener->handle(), nullptr, nullptr);
    auto incoming = kernel->object_table()->LookupObject<rex::system::XSocket>(accepted);
    Check(bool(incoming), "XAM accept allows omitted peer address");
    setsockopt(incoming->native_handle(), SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<char*>(&timeout), sizeof(timeout));
    Check(send(client, reinterpret_cast<char*>(payload), sizeof(payload), 0) == sizeof(payload), "TCP send");
    Check(incoming->Recv(received, sizeof(received), 0) == sizeof(payload) && !std::memcmp(payload, received, sizeof(payload)), "TCP accepted socket remains alive");
    closesocket(client);

    rex::system::xam::LanRecord record; record.title = kernel->title_id(); record.open_public = 15;
    record.info[0] = 0x81; record.info[7] = 0x76;
    rex::system::xam::LanSetContext(0, 0x8000, 7);
    Check(rex::system::xam::LanPublish(record), "LAN listener starts");
    auto found = rex::system::xam::LanSearch(record.title);
    Check(found.size() == 1 && found[0].contexts.at(0x8000) == 7 && found[0].open_public == 15, "Actual LAN discovery round trip");
    Check(rex::system::xam::LanSearch(0x98765432).empty(), "Other title excluded");
    // Exercise the real XGI ABI: results_buffer_size is an inline DWORD.
    put(0, 1); put(4, 0); put(8, 4); put(12, 1); put(16, 0); put(20, address + 800);
    put(24, 512); put(28, address + 1024); put(32, 1); put(800, 0x8000); put(804, 7); put(1536, 0x12345678);
    Check(call(0xB001C, 36) == 0 && get(1024) == 1 && get(1028) == address + 1032 && get(1536) == 0x12345678, "SearchEx inline size ABI and bounded results");
    put(804, 9); Check(call(0xB001C, 36) == 0 && get(1024) == 0, "SearchEx context filter");
    rex::system::xam::LanSessionId id{}; id[0] = 0x81; id[7] = 0x76;
    using namespace rex::system::xam;
    Check(LanQosListen(id, 5, {9, 27, 81}), "QoS listener enable and data");
    auto quality = LanQosProbe({LanQosTarget{0x7F000002, id}}, 3);
    Check(quality.size() == 1 && quality[0].contacted && !quality[0].disabled && quality[0].received == 3 && quality[0].sent == 3 &&
          quality[0].data == std::vector<uint8_t>({9, 27, 81}) && quality[0].minimum_ms >= 1, "QoS actual packet round trips and data");
    Check(LanQosListen(id, 2, {}), "QoS disable");
    quality = LanQosProbe({LanQosTarget{0x7F000002, id}}, 1);
    Check(quality[0].contacted && quality[0].disabled && quality[0].data.empty(), "Disabled QoS does not disclose cached data");
    Check(LanQosListen(id, 16, {}), "QoS release");
    quality = LanQosProbe({LanQosTarget{0x7F000002, id}}, 1);
    Check(!quality[0].contacted && quality[0].received == 0, "Released QoS cannot answer");
    rex::system::xam::LanRemove(id);
    Check(rex::system::xam::LanSearch(record.title).empty(), "Removed lobby is absent");
    mem->SystemHeapFree(address);
    std::cout << "PASS: profiles, legacy saves, session transitions/capacity, UDP byte order, TCP accept, LAN discovery/QoS\n";
    return 0;
  } catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
}
