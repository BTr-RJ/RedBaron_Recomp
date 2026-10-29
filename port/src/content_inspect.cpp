#include <rex/filesystem/devices/stfs_container_device.h>
#include <rex/logging.h>
#include <filesystem>
#include <iomanip>
#include <iostream>

// Read-only inspection. Installation is a separate operation and never runs here.
int wmain(int argc, wchar_t** argv) {
  if (argc < 2) return 2;
  rex::InitLogging();
  int failed=0;
  for (int i=1;i<argc;++i) {
    auto path=std::filesystem::path(argv[i]);
    auto header=rex::filesystem::StfsContainerDevice::ReadPackageHeader(path);
    if (!header) {std::cerr<<path.filename().string()<<": invalid STFS header\n";++failed;continue;}
    std::cout<<path.filename().string()<<" title="<<std::hex<<std::uppercase<<std::setw(8)
             <<std::setfill('0')<<uint32_t(header->metadata.execution_info.title_id)
             <<" type="<<std::setw(8)<<static_cast<uint32_t>(header->metadata.content_type.get())<<std::dec<<'\n';
  }
  return failed ? 1 : 0;
}
