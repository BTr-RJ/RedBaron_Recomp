#include <WinSock2.h>
#include "selection_admission.h"
#include <stdexcept>
#include <utility>
namespace redbaron::net {
SelectionAdmission::SelectionAdmission(SelectionGateway& gateway,SocketBridge& bridge,Wire& wire,Start start)
    :gateway_(gateway),bridge_(bridge),wire_(wire),start_(std::move(start)) {
  if(!start_ || gateway_.selected || gateway_.expired)throw std::invalid_argument("Selection gateway already in use or missing admission callback");
  gateway_.selected=[this](uint16_t room) {
    if(state_!=State::Idle)return;
    room_=room;state_=State::Waiting;admitted_=false;
    if(!start_(room)){gateway_.Cancel();state_=State::Failed;}
  };
  gateway_.expired=[this](uint16_t){admitted_=false;state_=State::Failed;};
}
SelectionAdmission::~SelectionAdmission(){gateway_.selected={};gateway_.expired={};gateway_.Cancel();}
bool SelectionAdmission::Complete(bool accepted) {
  if(state_!=State::Waiting)return false;
  if(!accepted){gateway_.Cancel();state_=State::Failed;return true;}
  admitted_=true;return true;
}
void SelectionAdmission::Reset(){gateway_.Cancel();state_=State::Idle;room_=0;admitted_=false;}
void SelectionAdmission::Poll() {
  gateway_.Poll();
  if((state_!=State::Waiting && state_!=State::Connected) || !admitted_ || !wire_.Ready(0))return;
  while(auto pending=gateway_.Take()) {
    if(pending->room!=room_) {closesocket(static_cast<SOCKET>(pending->socket));gateway_.Cancel();state_=State::Failed;return;}
    if(!bridge_.Adopt(0,pending->port,pending->socket)) {
      gateway_.Cancel();state_=State::Failed;return;
    }
    state_=State::Connected;
  }
}
}
