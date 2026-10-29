#include <eos_init.h>
#include <eos_version.h>
#include <eos_sdk.h>
#include <chrono>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>

static std::map<std::string, std::string> ReadConfig(const char* path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) throw std::runtime_error("Cannot read the EOS configuration file");
  std::map<std::string, std::string> values;
  std::string line;
  size_t total=0;
  bool first=true;
  while (std::getline(file,line)) {
    total+=line.size();
    if (total>4096) throw std::runtime_error("EOS configuration is too large");
    if (first && line.size()>=3 && line.compare(0,3,"\xEF\xBB\xBF")==0) line.erase(0,3);
    first=false;
    if (!line.empty() && line.back()=='\r') line.pop_back();
    if (line.empty() || line[0]=='#') continue;
    const auto equal=line.find('=');
    if (equal==std::string::npos || equal==0 || equal+1==line.size())
      throw std::runtime_error("Complete all EOS configuration fields");
    auto key=line.substr(0,equal), value=line.substr(equal+1);
    if (value.size()>256 || value.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_")!=std::string::npos ||
        !values.emplace(std::move(key),std::move(value)).second)
      throw std::runtime_error("Invalid EOS configuration field");
  }
  for (const char* name : {"product_id","sandbox_id","deployment_id","client_id","client_secret"})
    if (!values.contains(name)) throw std::runtime_error("Complete all EOS configuration fields");
  if (values.size()!=5) throw std::runtime_error("Unexpected EOS configuration field");
  return values;
}

// This executable is a separate diagnostic, not the game or online provider.
// SDK initialization itself requires neither a product credential nor login.
int main(int argc, char** argv) {
  const bool sdk_check=argc==2 && std::string_view(argv[1])=="--sdk-check";
  const bool platform_check=argc==3 && std::string_view(argv[1])=="--platform-check";
  if (!sdk_check && !platform_check) {
    std::cerr << "Usage: redbaron_eos_probe --sdk-check | --platform-check <config>\n";
    return 2;
  }
  std::map<std::string,std::string> config;
  if (platform_check) {
    try { config=ReadConfig(argv[2]); }
    catch(const std::exception& error) { std::cerr << error.what() << '\n'; return 2; }
  }
  EOS_InitializeOptions options{};
  options.ApiVersion = EOS_INITIALIZE_API_LATEST;
  options.ProductName = "RedBaron_Recomp";
  options.ProductVersion = "0.10-dev";
  const EOS_EResult initialized = EOS_Initialize(&options);
  if (initialized != EOS_EResult::EOS_Success) {
    std::cerr << "EOS SDK initialization failed: " << int(initialized) << '\n';
    return 1;
  }
  const char* version = EOS_GetVersion();
  std::cout << "EOS SDK initialized: " << (version ? version : "unknown version") << '\n';
  int status=0;
  if (platform_check) {
    EOS_Platform_Options platform{};
    platform.ApiVersion=EOS_PLATFORM_OPTIONS_API_LATEST;
    platform.ProductId=config.at("product_id").c_str();
    platform.SandboxId=config.at("sandbox_id").c_str();
    platform.DeploymentId=config.at("deployment_id").c_str();
    platform.ClientCredentials.ClientId=config.at("client_id").c_str();
    platform.ClientCredentials.ClientSecret=config.at("client_secret").c_str();
    platform.bIsServer=EOS_FALSE;
    platform.Flags=EOS_PF_DISABLE_OVERLAY;
    const auto handle=EOS_Platform_Create(&platform);
    if (!handle) {std::cerr << "EOS platform creation failed; check the local configuration\n"; status=1;}
    else {
      for (int i=0;i<10;++i) {EOS_Platform_Tick(handle);std::this_thread::sleep_for(std::chrono::milliseconds(10));}
      EOS_Platform_Release(handle);
      std::cout << "EOS platform created and released (no login was attempted)\n";
    }
  }
  const EOS_EResult shutdown = EOS_Shutdown();
  if (shutdown != EOS_EResult::EOS_Success) {
    std::cerr << "EOS SDK shutdown failed: " << int(shutdown) << '\n';
    return 1;
  }
  std::cout << "EOS SDK shutdown complete\n";
  return status;
}
