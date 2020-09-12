#define _USE_MATH_DEFINES
#include <cmath>
#include "Window.h"
#include "godec/ComponentGraph.h"
#include <boost/format.hpp>
#include <iomanip>

namespace Godec {

WindowComponent::~WindowComponent() {
}

LoopProcessor* WindowComponent::make(std::string id, ComponentGraphConfig* configPt) {
    return new WindowComponent(id, configPt);
}
std::string WindowComponent::describeThyself() {
    return "Chops incoming audio stream into windowed audio features";
}
WindowComponent::WindowComponent(std::string id, ComponentGraphConfig* configPt) :
    LoopProcessor(id,configPt) {
    addInputSlotAndUUID(SlotStreamedAudio, UUID_AudioDecoderMessage);

    std::list<std::string> requiredOutputSlots;
    requiredOutputSlots.push_back(SlotWindowedAudio);
    initOutputs(requiredOutputSlots);

    /* initialize sampling parameters for the current waveform */
    mLowLatency = configPt->get<bool>("low_latency", "Low-latency mode (assumes never-ending audio, can not be used in offline where utterances end!)");
    mSamplingRate = configPt->get<float>("sampling_frequency", "Source sampling rate");
    float local_fRate = configPt->get<float>("analysis_frame_step_size", "Analysis frame step size");
    float local_winDur = configPt->get<float>("analysis_frame_size", "Analysis frame size in milliseconds");
    std::string windowingFunction = configPt->get<std::string>("windowing_function", "Windowing function (hamming,blackman, rectangle)");
    windowSize = (int)round(0.001 * mSamplingRate * local_winDur);    /* points in window */
    stepSize = (int)round(0.001 * mSamplingRate * local_fRate);     /* points per shift */
    window = Vector(windowSize);
    if (windowingFunction == "hamming") {
        for(int idx = 0; idx < windowSize; idx++) {
            window(idx) = 0.54-0.46*cos(2.0*M_PI/(windowSize-1));
        }
    } else if (windowingFunction == "rectangle") {
        window.setConstant(1.0);
    } else {
        GODEC_ERR << "Not implemented yet!";
    }
    zeroMean = AccumCovariance::make(1, Diagonal, true, false);

    mUttReceivedAudio = 0;
    mUttStartStreamOffset = 0;
    mAccumAudioOffsetInUtt = 0;
    mProcessPointerInAccumAudio = 0;
}

void WindowComponent::ProcessMessage(const DecoderMessageBlock& msgBlock) {
    auto convStateMsg =msgBlock.get<ConversationStateDecoderMessage>(SlotConversationState);
    auto audioMsg =msgBlock.get<AudioDecoderMessage>(SlotStreamedAudio);
    if (mSamplingRate != audioMsg->mSampleRate) {
        GODEC_ERR << getLPId() << ": Expected sampling rate " << mSamplingRate << ", got " << audioMsg->mSampleRate << std::endl;
    }

    float ticksPerSample = audioMsg->mTicksPerSample;
    const Vector& audio = audioMsg->mAudio;
    mAccumAudio.conservativeResize(mAccumAudio.size()+audio.size());
    mAccumAudio.tail(audio.size()) = audio;
    mUttReceivedAudio += audio.size();

    int audioHoldoff = (mLowLatency || convStateMsg->mLastChunkInUtt) ? 0 : stepSize; // Problem is, we need to keep at least one window of audio around so that the last message can be tagged with the EOM boolean
    int nFrames = std::max(0.0, floor(((int)mAccumAudio.size() - mProcessPointerInAccumAudio - audioHoldoff) / (double)stepSize));
    if (nFrames == 0) {
        if (!convStateMsg->mLastChunkInUtt) return;
        else if (mLowLatency) GODEC_ERR << "You ran in low-latency mode but ended the utterance, this is not allowed";
        else GODEC_ERR << "We should never end up here";
    }

    Matrix outMat(windowSize, nFrames);
    std::vector<uint64_t> outTimestamps;
    int frameIdx = -1;

    Vector rawAudioSnippet(windowSize);
    while ((mProcessPointerInAccumAudio + stepSize) <= ((int)mAccumAudio.size() - audioHoldoff)) {
        frameIdx++;
        mProcessPointerInAccumAudio += stepSize;

        rawAudioSnippet.setZero();
        int64_t pickupStart = std::max((int64_t)0,mProcessPointerInAccumAudio-windowSize);
        int64_t pickupSize = mProcessPointerInAccumAudio-pickupStart;
        rawAudioSnippet.tail(mProcessPointerInAccumAudio-pickupStart) = mAccumAudio.segment(pickupStart, pickupSize);

        zeroMean->reset();
        zeroMean->addData(rawAudioSnippet);
        Vector normAudio = zeroMean->normalize(rawAudioSnippet);
        Vector filteredAudio = window.cwiseProduct(normAudio);

        outMat.col(frameIdx) = filteredAudio;
        uint64_t frameTimestamp = mUttStartStreamOffset + (int64_t)round(ticksPerSample*(mProcessPointerInAccumAudio+mAccumAudioOffsetInUtt));
        outTimestamps.push_back(frameTimestamp);
    }

    if (outTimestamps.size() != nFrames) {
        GODEC_ERR << "Number of frames was not estimated correctly. " << outTimestamps.size() << " vs " << nFrames << std::endl;
    }

    if (convStateMsg->mLastChunkInUtt && outTimestamps.size() > 0) {
        outTimestamps.back() = convStateMsg->getTime();
    }

    boost::format fmter("WINAUDIO[0:%1%]%%f");
    fmter % (windowSize - 1);

    DecoderMessage_ptr featMsg = FeaturesDecoderMessage::create(
                                     outTimestamps.back(), convStateMsg->mUtteranceId,
                                     outMat, fmter.str(), outTimestamps);
    if (audioMsg->getDescriptor("vtl_stretch") != "") {
        (boost::const_pointer_cast<DecoderMessage>(featMsg))->addDescriptor("vtl_stretch", audioMsg->getDescriptor("vtl_stretch"));
    }
    pushToOutputs(SlotWindowedAudio, featMsg);

    if (mProcessPointerInAccumAudio > 10*stepSize) {
        int64_t framesToRemove = mAccumAudio.size()-2*windowSize;
        mAccumAudio = (Vector)mAccumAudio.tail(mAccumAudio.size()-framesToRemove);
        mProcessPointerInAccumAudio -= framesToRemove;
        mAccumAudioOffsetInUtt += framesToRemove;
    }

    if (convStateMsg->mLastChunkInUtt) {
        mUttStartStreamOffset = featMsg->getTime()+1;
        mProcessPointerInAccumAudio = -1;
        mUttReceivedAudio = 0;
        mAccumAudioOffsetInUtt = 0;
        mAccumAudio = Vector();
    }
}

}
