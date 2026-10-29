#include "roster.h"
#include <iostream>
#include <stdexcept>
using namespace redbaron::eos;
void Check(bool ok){if(!ok)throw std::runtime_error("EOS roster test failed");}
int main(){try {
  Roster empty{};std::vector<std::string> members{"owner","one","two","three","four","five","six","seven"};
  auto full=AssignRoster(empty,"owner",members);Check(full && (*full)[0]=="owner" && (*full)[7]=="seven");
  Check(DecodeRoster(EncodeRoster(*full),"owner",members)==full);
  auto changed=members;changed.erase(changed.begin()+2);changed.push_back("new");
  auto next=AssignRoster(*full,"owner",changed);Check(next && (*next)[2]=="new" && (*next)[7]=="seven");
  Check(!DecodeRoster(EncodeRoster(*full),"owner",changed));
  auto forged=*full;forged[2]="intruder";Check(!DecodeRoster(EncodeRoster(forged),"owner",members));
  forged=*full;forged[2]=forged[1];Check(!DecodeRoster(EncodeRoster(forged),"owner",members));
  Check(!DecodeRoster({"owner,one,two", "x"},"owner",members));
  members.push_back("ninth");Check(!AssignRoster(empty,"owner",members));
  std::cout<<"PASS: stable eight-player host roster and member validation\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
