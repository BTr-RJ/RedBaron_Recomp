// Local metadata extraction only. Never execute guest code or upload game data.
#include <rex/runtime.h>
#include <rex/logging.h>
#include <rex/system/kernel_state.h>
#include <rex/system/util/xdbf_utils.h>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
static std::string Quote(std::string_view text) {
  std::string out="\"";for(unsigned char c:text) {
    if(c=='"' || c=='\\'){out+='\\';out+=char(c);}else if(c>=32)out+=char(c);else out+=' ';
  }return out+'"';
}
int main(int argc,char** argv) {
  if(argc!=3){std::cerr<<"Usage: redbaron_achievement_export <local-game-folder> <empty-output-folder>\n";return 2;}
  try {
    const auto game=std::filesystem::absolute(argv[1]),out=std::filesystem::absolute(argv[2]);
    if(std::filesystem::exists(out))throw std::runtime_error("Use a new output folder to preserve existing files");
    std::filesystem::create_directories(out/"icons");rex::InitLogging();
    rex::Runtime runtime(game,out/"runtime");rex::RuntimeConfig config;config.tool_mode=true;
    if(runtime.Setup(std::move(config)) || runtime.LoadXexImage("game:\\default.xex"))throw std::runtime_error("Cannot load local XEX metadata");
    auto* kernel=runtime.kernel_state();if(kernel->title_id()!=0x584109B5)throw std::runtime_error("Unexpected title ID");
    const auto all=kernel->loaded_achievements();if(all.empty())throw std::runtime_error("No native achievement definitions found");
    const auto db=kernel->title_xdbf();std::ofstream json(out/"achievement-catalog.json",std::ios::binary);
    json<<"{\"titleId\":\"584109B5\",\"achievements\":[";bool first=true;
    for(const auto& a:all) {
      if(!first)json<<',';first=false;char id[32];std::snprintf(id,sizeof(id),"SFA_%08X",a.id);
      std::string icon;auto data=db.GetEntry(rex::system::util::XdbfSection::kImage,a.image_id);
      if(data && data.size>=8 && data.buffer[0]==137 && data.buffer[1]=='P' && data.buffer[2]=='N' && data.buffer[3]=='G') {
        icon="icons/"+std::string(id)+".png";std::ofstream image(out/icon,std::ios::binary);image.write(reinterpret_cast<const char*>(data.buffer),data.size);
      }
      json<<"{\"id\":"<<a.id<<",\"eosId\":"<<Quote(id)<<",\"label\":"<<Quote(a.label)
        <<",\"description\":"<<Quote(a.description)<<",\"lockedDescription\":"<<Quote(a.unachieved_description)
        <<",\"gamerscore\":"<<a.gamerscore<<",\"flags\":"<<a.flags<<",\"icon\":"<<Quote(icon)<<'}';
    }
    json<<"]}";json.flush();if(!json)throw std::runtime_error("Cannot write achievement catalog");
    std::cout<<"Exported "<<all.size()<<" native achievements locally; keep this folder out of Git/public packages\n";return 0;
  }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
