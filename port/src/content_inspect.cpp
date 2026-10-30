#include <rex/filesystem/devices/stfs_container_device.h>
#include <rex/logging.h>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <queue>
#include <fstream>
#include <vector>
#include <rex/filesystem/file.h>
#include <rex/string.h>
#include <rex/system/xam/content_manager.h>
#include <rex/system/xam/content_device.h>
#include <rex/runtime.h>
#include <Windows.h>
#include <cwctype>

// Inspection and extraction to a new, explicit staging directory. This tool
// does not change the active game installation or claim to install content.
int wmain(int argc, wchar_t** argv) {
  if (argc < 2) return 2;
  rex::InitLogging();
  if(std::wstring_view(argv[1])==L"--verify") {
    if(argc!=3)return 2;
    rex::Runtime runtime({},std::filesystem::path(argv[2]));
    rex::RuntimeConfig config;config.tool_mode=true;
    if(runtime.Setup(std::move(config))!=0){std::cerr<<"Runtime setup failed\n";return 1;}
    auto* manager=runtime.kernel_state()->content_manager();
    auto entries=manager->ListContent(static_cast<uint32_t>(rex::system::xam::DummyDeviceId::HDD),0,
        rex::system::XContentType::kMarketplaceContent,0x584109B5);
    for(const auto& entry:entries) {
      uint32_t license=0;
      if(manager->OpenContent("redbaron_verify",0,entry,license)!=0) {std::cerr<<"Could not mount content\n";return 1;}
      std::cout<<entry.file_name()<<" license="<<license<<std::endl;
      if(manager->CloseContent("redbaron_verify")!=0) {std::cerr<<"Could not close content\n";return 1;}
    }
    std::cout<<"Recognized content packages: "<<entries.size()<<std::endl;return 0;
  }
  const bool make_header=std::wstring_view(argv[1])==L"--header";
  const bool extract_game=std::wstring_view(argv[1])==L"--extract-game";
  const bool extract=std::wstring_view(argv[1])==L"--extract" || extract_game;
  const bool list=std::wstring_view(argv[1])==L"--list" || extract;
  if ((list && argc<3) || ((extract || make_header) && argc!=4)) return 2;
  std::filesystem::path destination;
  if(extract) {
    destination=std::filesystem::absolute(argv[2]);
    if(std::filesystem::exists(destination)) {std::cerr<<"Extraction destination already exists\n";return 1;}
    for(auto parent=destination.parent_path();!parent.empty();) {
      const auto attrs=GetFileAttributesW(parent.c_str());
      if(attrs==INVALID_FILE_ATTRIBUTES || (attrs & FILE_ATTRIBUTE_REPARSE_POINT)) {
        std::cerr<<"Extraction parent must exist and cannot contain links\n";return 1;
      }
      auto next=parent.parent_path();if(next==parent)break;parent=next;
    }
  }
  int failed=0;
  for (int i=(extract || make_header)?3:(list?2:1);i<argc;++i) {
    auto path=std::filesystem::path(argv[i]);
    auto header=rex::filesystem::StfsContainerDevice::ReadPackageHeader(path);
    if (!header) {std::cerr<<path.filename().string()<<": invalid STFS header\n";++failed;continue;}
    if ((extract || make_header) && (uint32_t(header->metadata.execution_info.title_id)!=0x584109B5 ||
        static_cast<uint32_t>(header->metadata.content_type.get())!=(extract_game?0x000D0000u:2u))) {
      std::cerr<<(extract_game?"Package is not an Arcade game for this title\n":"Package is not marketplace content for this title\n");return 1;
    }
    if(make_header) {
      rex::system::xam::XCONTENT_AGGREGATE_DATA data{};
      data.device_id=static_cast<uint32_t>(rex::system::xam::DummyDeviceId::HDD);
      data.content_type=rex::system::XContentType::kMarketplaceContent;
      data.title_id=0x584109B5;data.xuid=0;
      data.set_file_name(rex::path_to_utf8(path.filename()));
      auto name=header->metadata.display_name(rex::system::XLanguage::kEnglish);
      data.set_display_name(name.empty()?rex::path_to_utf16(path.filename()):name);
      uint32_t license=0;
      for(const auto& entry:header->header.licenses)if(entry.license_flags)license|=entry.license_bits;
      HANDLE output=CreateFileW(argv[2],GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
      if(output==INVALID_HANDLE_VALUE){std::cerr<<"Cannot create a new content header\n";return 1;}
      DWORD written=0;bool ok=WriteFile(output,&data,sizeof(data),&written,nullptr) && written==sizeof(data);
      if(ok && license)ok=WriteFile(output,&license,sizeof(license),&written,nullptr) && written==sizeof(license);
      ok=FlushFileBuffers(output) && ok;CloseHandle(output);
      if(!ok){std::cerr<<"Cannot write content header\n";return 1;}
      continue;
    }
    std::cout<<path.filename().string()<<" title="<<std::hex<<std::uppercase<<std::setw(8)
             <<std::setfill('0')<<uint32_t(header->metadata.execution_info.title_id)
             <<" type="<<std::setw(8)<<static_cast<uint32_t>(header->metadata.content_type.get())<<std::dec<<std::endl;
    if (list) {
      rex::filesystem::StfsContainerDevice device("",path);
      if (!device.Initialize()) {std::cerr<<"Could not open package\n";++failed;continue;}
      auto* root=device.ResolvePath("");
      if (!root) {std::cerr<<"Package has no root\n";++failed;continue;}
      uint64_t total_bytes=0,extracted_bytes=0;
      if(extract) {
        std::queue<rex::filesystem::Entry*> pending;pending.push(root);
        while(!pending.empty()){auto* item=pending.front();pending.pop();for(auto& child:item->children())pending.push(child.get());if(!(item->attributes() & rex::filesystem::kFileAttributeDirectory))total_bytes+=item->size();}
      }
      const auto report=[&](){if(extract)std::cout<<"@@RBPROGRESS {\"stage\":\"Extracting "<<(extract_game?"game":"DLC")<<" package\",\"completed\":"<<extracted_bytes<<",\"total\":"<<total_bytes<<"}"<<std::endl;};
      report();
      if(extract && !std::filesystem::create_directory(destination)) {std::cerr<<"Cannot create destination\n";return 1;}
      std::queue<rex::filesystem::Entry*> entries;entries.push(root);
      while(!entries.empty()) {
        auto* entry=entries.front();entries.pop();
        for (auto& child:entry->children()) entries.push(child.get());
        if(entry==root)continue;
        std::cout<<"  "<<entry->path()<<" ("<<entry->size()<<" bytes)"<<std::endl;
        if(!extract)continue;
        auto relative=rex::to_path(rex::string::utf8_fix_path_separators(entry->path()));
        if(relative.empty() || relative.has_root_path()) {std::cerr<<"Unsafe package path\n";return 1;}
        for(const auto& part:relative) {
          auto name=part.wstring();
          if(name.empty() || name==L".." || name==L"." || name.back()==L'.' || name.back()==L' ' ||
             name.find_first_of(L"<>:\"|?*")!=std::wstring::npos) {std::cerr<<"Unsafe package path\n";return 1;}
          for(auto ch:name)if(ch<32){std::cerr<<"Unsafe package path\n";return 1;}
          auto stem=name.substr(0,name.find(L'.'));
          for(auto& ch:stem)ch=std::towupper(ch);
          if(stem==L"CON" || stem==L"PRN" || stem==L"AUX" || stem==L"NUL" ||
             (stem.size()==4 && (stem.starts_with(L"COM") || stem.starts_with(L"LPT")) && stem[3]>=L'1' && stem[3]<=L'9')) {
            std::cerr<<"Reserved package path\n";return 1;
          }
        }
        auto output=destination/relative;
        if(entry->attributes() & rex::filesystem::kFileAttributeDirectory) {
          std::filesystem::create_directories(output);continue;
        }
        std::filesystem::create_directories(output.parent_path());
        if(std::filesystem::exists(output)) {std::cerr<<"Duplicate package path\n";return 1;}
        rex::filesystem::File* file=nullptr;
        if(entry->Open(rex::filesystem::FileAccess::kFileReadData,&file)!=0) {std::cerr<<"Cannot open package entry\n";return 1;}
        std::ofstream out(output,std::ios::binary);std::vector<uint8_t> buffer(65536);
        size_t offset=0;
        while(out && offset<entry->size()) {
          size_t received=0, n=std::min(buffer.size(),size_t(entry->size()-offset));
          auto status=file->ReadSync(std::span<uint8_t>(buffer.data(),n),offset,&received);
          if(status!=0 || !received || received>n) break;
          out.write(reinterpret_cast<const char*>(buffer.data()),received);offset+=received;
        }
        file->Destroy();out.flush();
        if(!out || offset!=entry->size()) {std::cerr<<"Incomplete package entry\n";return 1;}
        extracted_bytes+=offset;report();
      }
    }
  }
  return failed ? 1 : 0;
}
