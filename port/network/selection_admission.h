#pragma once
#include "selection_gateway.h"
#include "socket_bridge.h"
#include <functional>

namespace redbaron::net {
// Coordinates the TCP selection with an asynchronous room provider. The
// owner drives EOS and calls Complete only after admission and peer setup.
class SelectionAdmission {
 public:
  using Start=std::function<bool(uint16_t)>;
  enum class State {Idle,Waiting,Connected,Failed};
  SelectionAdmission(SelectionGateway& gateway,SocketBridge& bridge,Wire& wire,Start start);
  ~SelectionAdmission();
  SelectionAdmission(const SelectionAdmission&)=delete;
  SelectionAdmission& operator=(const SelectionAdmission&)=delete;
  void Poll();
  // False aborts pending game sockets; true permits transfer once peer is ready.
  bool Complete(bool accepted);
  void Reset();
  State state() const{return state_;}
  uint16_t room() const{return room_;}
 private:
  SelectionGateway& gateway_;SocketBridge& bridge_;Wire& wire_;Start start_;
  State state_=State::Idle;
  uint16_t room_=0;
  bool admitted_=false;
};
}
