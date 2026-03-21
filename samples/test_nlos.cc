#include "ns3/propagation-loss-model.h"
#include "ns3/mobility-model.h"
using namespace ns3;
class NlosPropagationLossModel : public PropagationLossModel {
public:
  static TypeId GetTypeId (void) {
    static TypeId tid = TypeId ("ns3::NlosPropagationLossModel")
      .SetParent<PropagationLossModel> ()
      .SetGroupName ("Propagation")
      .AddConstructor<NlosPropagationLossModel> ();
    return tid;
  }
  NlosPropagationLossModel() {}
  double DoCalcRxPower (double txPowerDbm, Ptr<MobilityModel> a, Ptr<MobilityModel> b) const override {
      return txPowerDbm;
  }
  int64_t DoAssignStreams (int64_t stream) override { return 0; }
};
