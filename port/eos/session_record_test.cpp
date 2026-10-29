#include "session_record.h"
#include <iostream>
#include <stdexcept>
using namespace redbaron::eos;
void Check(bool b) {if(!b)throw std::runtime_error("Session record codec failed");}
int main() {
  try {
    for(size_t n:{size_t(1),size_t(104),size_t(155),size_t(156),size_t(157),size_t(8192)}) {
      std::string hex;for(size_t i=0;i<n;++i) {static constexpr char h[]="0123456789abcdef";hex+=h[(i>>4)&15];hex+=h[i&15];}
      auto chunks=EncodeSessionRecord(hex);Check(chunks.size()<=53);Check(DecodeSessionRecord(chunks)==hex);
    }
    Check(!DecodeSessionRecord({}));Check(!DecodeSessionRecord({"!x"}));
    Check(!DecodeSessionRecord({"A"}));Check(!DecodeSessionRecord({"AB"}));
    Check(!DecodeSessionRecord({"AA","AA"}));
    bool failed=false;try {EncodeSessionRecord("0z");}catch(const std::invalid_argument&) {failed=true;}Check(failed);
    std::cout<<"PASS: bounded session record encoding and malformed input rejection\n";return 0;
  } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
