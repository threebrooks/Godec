#pragma once

#include "AccumCovariance.h"
#include <godec/ChannelMessenger.h>
#include "GodecMessages.h"

namespace Godec {

class WindowComponent : public LoopProcessor {
  public:
    static LoopProcessor* make(std::string id, ComponentGraphConfig* configPt);
    static std::string describeThyself();
    WindowComponent(std::string id, ComponentGraphConfig* configPt);
    ~WindowComponent();

  private:
    void ProcessMessage(const DecoderMessageBlock& msgBlock);
    Vector window;
    boost::shared_ptr<AccumCovariance> zeroMean;

    Vector mAccumAudio;
    bool mLowLatency;
    float mSamplingRate;
    int64_t mUttReceivedAudio;
    int64_t mAccumAudioOffsetInUtt;
    int64_t mUttStartStreamOffset;
    int64_t mProcessPointerInAccumAudio;

    int wLen;
    int fLen;
};

}
